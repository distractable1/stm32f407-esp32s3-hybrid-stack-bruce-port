"""Create, check, build and package apps for the T100 STM32 SD launcher.

This tool never flashes hardware. Only the bundled, protected SDK is supported;
edit Src/app.c and Src/app.h, or add application source files beneath Src.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET


APP_BASE = 0x08020000
APP_LIMIT = 0xDFFC0
RAM_TOP = 0x2001FF00
BOARD = 0x54313030
ABI = "t100-live-handoff-v1"
TEMPLATE_PROJECT = "STM32 Launcher Template"
APP_ID = re.compile(r"[A-Z][A-Z0-9_]{0,7}\Z")
EXCLUDED_DIRS = {"Debug", "Release", ".git", ".svn", ".metadata", "__pycache__",
                 ".pytest_cache", ".mypy_cache", ".build-temp", "build", "dist", "products"}
EDITABLE_FILES = {"Src/app.c", "Src/app.h", "README.md"}
REQUIRED_SDK = {"Src/main.c", "Src/board_support.c", "Src/board_support.h",
                "Src/sd_diskio.c", "Startup/startup_stm32f407zetx.s",
                "STM32F407ZETX_FLASH.ld", ".project", ".cproject"}


class FactoryError(ValueError):
    """A request could not be completed without violating the app contract."""


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _read_json(path: Path) -> dict:
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise FactoryError(f"Cannot read {path.name}: {error}") from error
    if not isinstance(value, dict):
        raise FactoryError(f"{path.name} must contain a JSON object")
    return value


def _write_json(path: Path, value: dict) -> None:
    # Paths are checked before this helper; exclusive temporary files prevent
    # replacing unrelated temporary names and os.replace publishes atomically.
    fd, temporary = tempfile.mkstemp(prefix=".factory-", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as stream:
            json.dump(value, stream, indent=2, sort_keys=True)
            stream.write("\n")
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def _is_link(path: Path) -> bool:
    try:
        info = path.lstat()
    except FileNotFoundError:
        return False
    # Windows directory junctions are reparse points, but not is_symlink().
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & 0x400)


def _no_links(path: Path) -> None:
    absolute = Path(os.path.abspath(path))
    for component in reversed((absolute, *absolute.parents)):
        if _is_link(component):
            raise FactoryError(f"Symlinks and reparse points are not allowed: {component}")


def _contained(root: Path, path: Path) -> Path:
    _no_links(root)
    _no_links(path)
    resolved = path.resolve()
    try:
        resolved.relative_to(root.resolve())
    except ValueError as error:
        raise FactoryError("Path escapes the configured apps root") from error
    return resolved


def _source_files(root: Path):
    _no_links(root)
    if not root.is_dir():
        raise FactoryError(f"Project directory is missing: {root}")
    for folder, directories, files in os.walk(root, followlinks=False):
        directories.sort()
        files.sort()
        for name in directories + files:
            candidate = Path(folder, name)
            if _is_link(candidate):
                raise FactoryError(f"Symlinks and reparse points are not allowed: {candidate}")
        directories[:] = [name for name in directories if name not in EXCLUDED_DIRS]
        for name in files:
            if name.startswith(".factory-") or name.endswith((".pyc", ".pyo")):
                continue
            path = Path(folder, name)
            if not path.is_file():
                raise FactoryError(f"Only regular project files are allowed: {path}")
            yield path.relative_to(root).as_posix(), path


def _source_hashes(project: Path) -> dict[str, str]:
    return {name: _sha(path.read_bytes()) for name, path in _source_files(project)}


def _copy_source(source: Path, destination: Path) -> None:
    for name, path in _source_files(source):
        target = _contained(destination, destination / name)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)


def _xml(path: Path) -> ET.Element:
    data = path.read_bytes()
    if b"<!DOCTYPE" in data.upper() or b"<!ENTITY" in data.upper():
        raise FactoryError(f"XML declarations are not allowed in {path.name}")
    try:
        return ET.fromstring(data)
    except ET.ParseError as error:
        raise FactoryError(f"Invalid {path.name}: {error}") from error


def _linker_number(value: str) -> int:
    match = re.fullmatch(r"\s*(0[xX][0-9a-fA-F]+|[0-9]+)\s*([KkMm]?)\s*", value)
    if not match:
        raise FactoryError("Linker MEMORY entries must use literal byte, K, or M sizes")
    base = 16 if match[1].lower().startswith("0x") else 10
    return int(match[1], base) * {"": 1, "K": 1024, "M": 1024 * 1024}[match[2].upper()]


def _check_linker(path: Path) -> None:
    text = re.sub(r"/\*.*?\*/", "", path.read_text(encoding="utf-8"), flags=re.S)
    regions = {}
    for region, origin, length in re.findall(
            r"\b(FLASH|RAM|CCMRAM)\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*([^,\n]+),\s*LENGTH\s*=\s*([^\n}]+)", text):
        if region in regions:
            raise FactoryError(f"Duplicate linker region {region}")
        regions[region] = (_linker_number(origin), _linker_number(length))
    if regions.get("FLASH") != (APP_BASE, APP_LIMIT):
        raise FactoryError("FLASH must start at 0x08020000 and have length 0xDFFC0")
    if regions.get("RAM") != (0x20000000, 0x1FF00):
        raise FactoryError("RAM must reserve the launcher's top 256 bytes")
    if regions.get("CCMRAM") != (0x10000000, 0x10000):
        raise FactoryError("CCMRAM must retain the SDK memory map")


def _verify_elf(elf: bytes, raw: bytes) -> dict:
    """Check ELF32 ARM load regions and their exact exported binary bytes."""
    def require(condition, message):
        if not condition:
            raise FactoryError(message)

    def within(offset, length):
        return 0 <= offset <= len(elf) and 0 <= length <= len(elf) - offset

    require(len(elf) >= 52, "ELF header is truncated")
    header = struct.unpack_from("<16sHHIIIIIHHHHHH", elf)
    require(header[0][:7] == b"\x7fELF\x01\x01\x01", "Expected a little-endian ELF32 file")
    require(header[1] == 2 and header[2] == 40 and header[3] == 1, "Expected an ARM executable ELF")
    require(header[8] == 52 and header[9] == 32 and 1 <= header[10] <= 64,
            "ELF program-header table is invalid")
    require(within(header[5], header[9] * header[10]), "ELF program headers are truncated")
    require(8 <= len(raw) <= APP_LIMIT, "BIN length is outside the application slot")
    stack, reset = struct.unpack_from("<II", raw)
    require(stack == RAM_TOP, "Initial stack must be 0x2001FF00")
    require(reset & 1 and APP_BASE + 8 <= reset & ~1 < APP_BASE + len(raw),
            "Reset vector is outside the relocated application")
    require(header[4] == reset, "ELF entry point does not match its reset vector")
    loaded = []
    runtime = []
    for index in range(header[10]):
        kind, offset, virtual, physical, filesz, memsz, flags, alignment = struct.unpack_from(
            "<8I", elf, header[5] + index * 32)
        if kind != 1:
            continue
        require(filesz <= memsz and within(offset, filesz), "ELF load segment is truncated")
        if not memsz:
            continue
        require(not (flags & 1 and flags & 2), "Writable executable ELF segments are not allowed")
        end = virtual + memsz
        if 0x20000000 <= virtual < RAM_TOP:
            require(end <= RAM_TOP, "ELF segment overlaps the reserved board handoff")
        elif 0x10000000 <= virtual < 0x10010000:
            require(end <= 0x10010000, "ELF segment exceeds CCMRAM")
        else:
            require(APP_BASE <= virtual and end <= APP_BASE + APP_LIMIT,
                    "ELF segment leaves the application memory map")
        require(all(end <= start or virtual >= stop for start, stop in runtime),
                "ELF runtime segments overlap")
        runtime.append((virtual, end))
        if filesz:
            require(APP_BASE <= physical and physical + filesz <= APP_BASE + len(raw),
                    "ELF load bytes leave the application binary")
            require(all(physical + filesz <= start or physical >= stop for start, stop in loaded),
                    "ELF flash load segments overlap")
            loaded.append((physical, physical + filesz))
            require(raw[physical - APP_BASE:physical - APP_BASE + filesz] == elf[offset:offset + filesz],
                    "BIN does not match its ELF load segment")
    require(bool(loaded) and min(start for start, _ in loaded) == APP_BASE and
            max(end for _, end in loaded) == APP_BASE + len(raw),
            "BIN contains bytes outside its ELF load extent")
    require(header[11] == 40 and 1 <= header[12] <= 4096 and
            within(header[6], header[11] * header[12]), "ELF section table is invalid")
    sections = [struct.unpack_from("<10I", elf, header[6] + index * 40)
                for index in range(header[12])]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        require(section[9] == 16 and section[5] % 16 == 0 and within(section[4], section[5]),
                "ELF symbol table is invalid")
        require(section[6] < len(sections), "ELF symbol strings are missing")
        strings = sections[section[6]]
        require(within(strings[4], strings[5]), "ELF symbol strings are truncated")
        names = elf[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], 16):
            name, value, _, _, _, _ = struct.unpack_from("<IIIBBH", elf, offset)
            require(name < len(names), "ELF symbol name offset is invalid")
            end = names.find(b"\0", name)
            require(end >= name, "ELF symbol name is unterminated")
            text = names[name:end].decode("ascii", errors="replace")
            if text in {"g_pfnVectors", "_estack", "_ebss", "Reset_Handler"}:
                symbols[text] = value
    require(symbols.get("g_pfnVectors") == APP_BASE, "Vector symbol is not relocated")
    require(symbols.get("_estack") == RAM_TOP, "ELF stack symbol violates the handoff reservation")
    require(0x20000000 <= symbols.get("_ebss", 0) <= RAM_TOP - 0x400,
            "ELF leaves less than the SDK minimum stack space")
    require(symbols.get("Reset_Handler", 0) & ~1 == reset & ~1, "ELF reset symbol does not match BIN")
    return {"bytes": len(raw), "stack": stack, "reset": reset, "load_segments": len(loaded)}


class AppFactory:
    def __init__(self, apps_root: Path, template_root: Path | None = None):
        root = Path(os.path.abspath(Path(apps_root).expanduser()))
        _no_links(root)
        root.mkdir(parents=True, exist_ok=True)
        self.apps_root = root.resolve()
        template = template_root or Path(__file__).resolve().parent / "template"
        self.template_root = Path(os.path.abspath(Path(template).expanduser()))
        _no_links(self.template_root)

    def _app(self, app_id: str) -> Path:
        if not isinstance(app_id, str) or not APP_ID.fullmatch(app_id):
            raise FactoryError("App ID must be 1 to 8 uppercase letters/digits/underscores, starting with a letter")
        if app_id in {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)),
                      *(f"LPT{i}" for i in range(1, 10))}:
            raise FactoryError("App ID is a reserved Windows filename")
        return _contained(self.apps_root, self.apps_root / app_id)

    def _template_files(self, project_name: str) -> dict[str, bytes]:
        files = {name: path.read_bytes() for name, path in _source_files(self.template_root)}
        missing = (REQUIRED_SDK | {"Src/app.c", "Src/app.h"}) - files.keys()
        if missing:
            raise FactoryError("Template is incomplete: " + ", ".join(sorted(missing)))
        for name in (".project", ".cproject"):
            text = files[name].decode("utf-8")
            if TEMPLATE_PROJECT not in text:
                raise FactoryError(f"Template {name} does not contain {TEMPLATE_PROJECT}")
            files[name] = text.replace(TEMPLATE_PROJECT, project_name).encode("utf-8")
        if b'"APP_DISPLAY_NAME"' not in files["Src/app.c"]:
            raise FactoryError('Template Src/app.c lacks the "APP_DISPLAY_NAME" string literal')
        return files

    def status(self) -> dict:
        result = {"ok": True, "apps_root": str(self.apps_root), "template_root": str(self.template_root),
                  "board": BOARD, "abi": ABI, "configuration": "Debug", "hardware_access": False}
        try:
            self._template_files(TEMPLATE_PROJECT)
            result["template_ready"] = True
        except (FactoryError, OSError) as error:
            result["template_ready"] = False
            result["template_error"] = str(error)
        try:
            result["tools"] = {key: str(value) for key, value in self._tools().items()}
            result["build_ready"] = result["template_ready"]
        except FactoryError as error:
            result["build_ready"] = False
            result["tool_error"] = str(error)
        return result

    def list_apps(self) -> dict:
        apps = []
        for child in sorted(self.apps_root.iterdir()):
            if _is_link(child):
                raise FactoryError(f"Symlinks and reparse points are not allowed: {child}")
            if child.is_dir() and APP_ID.fullmatch(child.name):
                report = self.validate_app(child.name)
                apps.append({"app_id": child.name, "name": report.get("name", child.name),
                             "path": str(child), "valid": report["ok"], "errors": report["errors"]})
        return {"ok": True, "apps_root": str(self.apps_root), "apps": apps}

    def create_app(self, app_id: str, name: str) -> dict:
        project = self._app(app_id)
        if not isinstance(name, str) or not 1 <= len(name) <= 31 or any(ord(c) < 32 or ord(c) > 126 for c in name):
            raise FactoryError("Display name must contain 1 to 31 printable ASCII characters")
        if project.exists():
            raise FactoryError(f"App already exists; nothing overwritten: {app_id}")
        project_name = f"STM32 {app_id}"
        files = self._template_files(project_name)
        sdk_hashes = {filename: _sha(data) for filename, data in files.items() if filename not in EDITABLE_FILES}
        # Replace the entire quoted placeholder so quotes and backslashes in a
        # display name remain data inside exactly one C string literal.
        escaped_name = json.dumps(name, ensure_ascii=True).encode("ascii")
        files["Src/app.c"] = files["Src/app.c"].replace(b'"APP_DISPLAY_NAME"', escaped_name)
        project.mkdir(exist_ok=False)
        try:
            for filename, data in files.items():
                path = _contained(project, project / filename)
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
            metadata = {"schema": 1, "app_id": app_id, "name": name, "project_name": project_name,
                        "board": BOARD, "abi": ABI, "sdk_hashes": sdk_hashes}
            _write_json(project / "launcher-app.json", metadata)
            report = self.validate_app(app_id)
            if not report["ok"]:
                raise FactoryError("Generated app failed validation: " + "; ".join(report["errors"]))
        except Exception:
            # Remove only the newly created, contained tree owned by this call.
            self._remove_tree(project, self.apps_root)
            raise
        return {"ok": True, "app_id": app_id, "name": name, "project_name": project_name,
                "path": str(project), "editable": [str(project / "Src/app.c"), str(project / "Src/app.h")]}

    def validate_app(self, app_id: str) -> dict:
        project = self._app(app_id)
        errors = []
        result = {"ok": False, "app_id": app_id, "path": str(project), "errors": errors}
        try:
            files = _source_hashes(project)  # Also rejects links in all included paths.
            metadata = _read_json(_contained(project, project / "launcher-app.json"))
            result["name"] = metadata.get("name")
            expected = {"schema": 1, "app_id": app_id, "project_name": f"STM32 {app_id}",
                        "board": BOARD, "abi": ABI}
            for key, value in expected.items():
                if type(metadata.get(key)) is not type(value) or metadata.get(key) != value:
                    errors.append(f"Metadata {key} must be {value!r}")
            name = metadata.get("name")
            if not isinstance(name, str) or not 1 <= len(name) <= 31 or any(not 32 <= ord(c) <= 126 for c in name):
                errors.append("Metadata display name must be 1 to 31 printable ASCII characters")
            template = self._template_files(expected["project_name"])
            protected = {name: _sha(data) for name, data in template.items() if name not in EDITABLE_FILES}
            if metadata.get("sdk_hashes") != protected:
                errors.append("SDK hash manifest differs from the bundled template")
            for filename, digest in protected.items():
                if files.get(filename) != digest:
                    errors.append(f"Protected SDK file changed or missing: {filename}")
            for filename in ("Src/app.c", "Src/app.h"):
                if filename not in files:
                    errors.append(f"Editable application file is missing: {filename}")
            project_xml = _xml(project / ".project")
            if project_xml.findtext("name") != expected["project_name"]:
                errors.append("Eclipse project name does not match the app ID")
            if project_xml.find("linkedResources") is not None:
                errors.append("External Eclipse linked resources are not allowed")
            config = _xml(project / ".cproject")
            debug = [node for node in config.iter("configuration") if node.get("name") == "Debug"]
            if len(debug) != 1:
                errors.append("Exactly one Debug build configuration is required")
            values = {node.get("value") for node in config.iter("listOptionValue")}
            if not {"STM32F407ZGTx", "STM32F407xx"}.issubset(values):
                errors.append("SDK MCU defines STM32F407ZGTx and STM32F407xx are required")
            if not any(node.get("value") == "STM32F407ZGTx" and "target_mcu" in node.get("superClass", "")
                       for node in config.iter("option")):
                errors.append("CubeIDE target MCU must be STM32F407ZGTx")
            _check_linker(project / "STM32F407ZETX_FLASH.ld")
            # These exact, protected framework files supply VTOR initialization
            # and a physical/touch return-to-menu path. User C is not interpreted.
            framework = template["Src/main.c"]
            board = template["Src/board_support.c"]
            if b"board_return_to_launcher" not in framework or b"SCB->VTOR" not in board:
                errors.append("Bundled SDK lacks required VTOR/menu framework")
        except (FactoryError, OSError, UnicodeError) as error:
            errors.append(str(error))
        result["ok"] = not errors
        return result

    def _tools(self) -> dict[str, Path]:
        candidates = []
        configured = os.environ.get("STM32CUBEIDE_ROOT")
        if configured:
            candidates.append(Path(configured).expanduser())
        candidates += list(Path("C:/ST").glob("STM32CubeIDE_2.2*/STM32CubeIDE"))
        for root in candidates:
            root = root.resolve()
            try:
                product = (root / ".eclipseproduct").read_text(encoding="utf-8")
            except OSError:
                continue
            if not re.search(r"^version=2\.2\.[^\r\n]+$", product, flags=re.M):
                continue
            ide = root / "stm32cubeidec.exe"
            binaries = sorted(root.glob("plugins/com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.*/tools/bin/arm-none-eabi-objcopy.exe"))
            if ide.is_file() and binaries:
                return {"ide": ide, "objcopy": binaries[-1].resolve()}
        raise FactoryError("STM32CubeIDE 2.2/toolchain not found; set STM32CUBEIDE_ROOT to its STM32CubeIDE directory")

    def _run(self, args: list[str], cwd: Path, timeout: int = 600):
        options = {"cwd": str(cwd), "capture_output": True, "text": True, "errors": "replace",
                   "timeout": timeout, "shell": False}
        if os.name == "nt":
            options["creationflags"] = subprocess.CREATE_NO_WINDOW
        return subprocess.run([str(argument) for argument in args], **options)

    @staticmethod
    def _remove_tree(path: Path, root: Path) -> None:
        checked = _contained(root, path)
        if checked == root.resolve():
            raise FactoryError("Refusing to remove a workspace root")
        # os.walk checks reparse points before Python's recursive deletion.
        for folder, directories, files in os.walk(checked, followlinks=False):
            for name in directories + files:
                if _is_link(Path(folder, name)):
                    raise FactoryError("Refusing to remove a tree containing links/reparse points")
        shutil.rmtree(checked)

    def _products(self, project: Path, app_id: str) -> dict[str, Path]:
        return {"elf": _contained(project, project / "build" / f"{app_id}.elf"),
                "binary": _contained(project, project / "build" / f"{app_id}.bin"),
                "receipt": _contained(project, project / "build" / "receipt.json"),
                "log": _contained(project, project / "build" / "build.log"),
                "package": _contained(project, project / "dist" / f"{app_id}.APP")}

    def _invalidate(self, products: dict[str, Path]) -> None:
        for key in ("receipt", "elf", "binary", "package"):
            path = products[key]
            _no_links(path)
            if path.exists():
                if not path.is_file():
                    raise FactoryError(f"Factory output is not a regular file: {path}")
                path.unlink()

    def build_app(self, app_id: str, configuration: str = "Debug") -> dict:
        project = self._app(app_id)
        products = self._products(project, app_id)
        self._invalidate(products)
        if configuration != "Debug":
            raise FactoryError("Only the literal Debug configuration is supported")
        report = self.validate_app(app_id)
        if not report["ok"]:
            raise FactoryError("App validation failed: " + "; ".join(report["errors"]))
        tools = self._tools()
        sources = _source_hashes(project)
        build_root = _contained(self.apps_root, self.apps_root / ".build-temp")
        build_root.mkdir(exist_ok=True)
        temporary = Path(tempfile.mkdtemp(prefix=f"{app_id}-", dir=build_root))
        products["log"].parent.mkdir(exist_ok=True)
        log = []
        try:
            staged = temporary / "project"
            staged.mkdir()
            _copy_source(project, staged)
            if _source_hashes(staged) != sources:
                raise FactoryError("Sources changed while staging the build")
            workspace = temporary / "workspace"
            args = [str(tools["ide"]), "--launcher.suppressErrors", "-nosplash", "-application",
                    "org.eclipse.cdt.managedbuilder.core.headlessbuild", "-data", str(workspace),
                    "-import", str(staged), "-cleanBuild", f"STM32 {app_id}/Debug"]
            run = self._run(args, temporary)
            log.append("CubeIDE build\n" + (run.stdout or "") + (run.stderr or ""))
            if run.returncode:
                raise FactoryError(f"CubeIDE build failed (exit {run.returncode}); see {products['log']}")
            elf_path = _contained(staged, staged / "Debug" / f"STM32 {app_id}.elf")
            if not elf_path.is_file():
                raise FactoryError("CubeIDE returned success without producing the expected fresh ELF")
            raw_path = _contained(staged, staged / "Debug" / f"{app_id}.bin")
            export = self._run([str(tools["objcopy"]), "-O", "binary", str(elf_path), str(raw_path)], temporary)
            log.append("BIN export\n" + (export.stdout or "") + (export.stderr or ""))
            if export.returncode or not raw_path.is_file():
                raise FactoryError(f"BIN export failed; see {products['log']}")
            elf, raw = elf_path.read_bytes(), raw_path.read_bytes()
            verified = _verify_elf(elf, raw)
            current = _source_hashes(project)
            if current != sources or not self.validate_app(app_id)["ok"]:
                raise FactoryError("Sources changed during the build; build again before packaging")
            products["elf"].write_bytes(elf)
            products["binary"].write_bytes(raw)
            receipt = {"schema": 1, "app_id": app_id, "configuration": "Debug", "sources": sources,
                       "elf": f"build/{app_id}.elf", "binary": f"build/{app_id}.bin",
                       "elf_sha256": _sha(elf), "bin_sha256": _sha(raw), "verified": verified,
                       "built_utc": datetime.now(timezone.utc).isoformat()}
            _write_json(products["receipt"], receipt)
            return {"ok": True, "app_id": app_id, "configuration": "Debug", "bytes": len(raw),
                    "elf": str(products["elf"]), "binary": str(products["binary"]),
                    "receipt": str(products["receipt"]), "log": str(products["log"]),
                    "sha256": receipt["bin_sha256"]}
        except (OSError, subprocess.SubprocessError, FactoryError) as error:
            self._invalidate(products)
            if isinstance(error, subprocess.TimeoutExpired):
                for output in (error.stdout, error.stderr):
                    if output:
                        log.append(output.decode(errors="replace") if isinstance(output, bytes) else output)
            log.append(f"Factory error: {error}")
            raise FactoryError(str(error)) from error
        finally:
            products["log"].write_text("\n\n".join(log), encoding="utf-8")
            self._remove_tree(temporary, build_root)

    def package_app(self, app_id: str) -> dict:
        project = self._app(app_id)
        products = self._products(project, app_id)
        # Never leave an older package looking current after a failed request.
        if products["package"].exists():
            products["package"].unlink()
        report = self.validate_app(app_id)
        if not report["ok"]:
            raise FactoryError("App validation failed: " + "; ".join(report["errors"]))
        receipt = _read_json(products["receipt"])
        if receipt.get("schema") != 1 or receipt.get("app_id") != app_id or receipt.get("configuration") != "Debug":
            raise FactoryError("A successful Debug build receipt is required")
        if receipt.get("sources") != _source_hashes(project):
            raise FactoryError("Sources changed since the successful build; build again")
        if receipt.get("elf") != f"build/{app_id}.elf" or receipt.get("binary") != f"build/{app_id}.bin":
            raise FactoryError("Build receipt artifact paths are invalid")
        try:
            elf, raw = products["elf"].read_bytes(), products["binary"].read_bytes()
        except OSError as error:
            raise FactoryError("Build artifacts are missing; build again") from error
        if receipt.get("elf_sha256") != _sha(elf) or receipt.get("bin_sha256") != _sha(raw):
            raise FactoryError("Build artifact checksum changed; build again")
        _verify_elf(elf, raw)
        module_path = Path(__file__).resolve().with_name("package_launcher.py")
        _no_links(module_path)
        specification = importlib.util.spec_from_file_location("_launcher_package", module_path)
        if not specification or not specification.loader:
            raise FactoryError("Bundled package_launcher.py is unavailable")
        module = importlib.util.module_from_spec(specification)
        try:
            specification.loader.exec_module(module)
            package = module.package_app(raw, report["name"])
            module.validate_package(package)
        except (OSError, ValueError, AttributeError) as error:
            raise FactoryError(f"Cannot package application: {error}") from error
        if receipt["sources"] != _source_hashes(project):
            raise FactoryError("Sources changed while packaging; build again")
        products["package"].parent.mkdir(exist_ok=True)
        products["package"].write_bytes(package)
        return {"ok": True, "app_id": app_id, "path": str(products["package"]),
                "bytes": len(package), "payload_bytes": len(raw), "sha256": _sha(package)}


def main(argv: list[str] | None = None) -> int:
    default_root = os.environ.get("STM32_LAUNCHER_APPS", str(Path.home() / "Documents/Codex/STM32LauncherApps"))
    parser = argparse.ArgumentParser(description=__doc__, epilog="No command flashes or accesses the STM32. Edit Src/app.c and Src/app.h, build, then package.")
    parser.add_argument("--root", type=Path, default=Path(default_root), help="Managed app directory (default: STM32_LAUNCHER_APPS or Documents/Codex/STM32LauncherApps)")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("status", help="Report SDK and local CubeIDE/toolchain availability")
    commands.add_parser("list", help="List apps and compatibility results under the managed root")
    create = commands.add_parser("create", help="Create a new app without overwriting existing files")
    create.add_argument("app_id", help="1–8 uppercase letters/digits/underscores, starting with a letter")
    create.add_argument("name", help="Display name, 1–31 printable ASCII characters")
    for command in ("validate", "build", "package"):
        child = commands.add_parser(command, help={"validate": "Check metadata, memory map and protected SDK", "build": "Build Debug in an isolated workspace and verify ELF/BIN", "package": "Create dist/ID.APP from a current verified build"}[command])
        child.add_argument("app_id")
    args = parser.parse_args(argv)
    try:
        factory = AppFactory(args.root)
        if args.command == "status":
            result = factory.status()
        elif args.command == "list":
            result = factory.list_apps()
        elif args.command == "create":
            result = factory.create_app(args.app_id, args.name)
        else:
            result = getattr(factory, f"{args.command}_app")(args.app_id)
        print(json.dumps(result, ensure_ascii=True))
        return 0 if result.get("ok") else 2
    except (FactoryError, OSError) as error:
        print(json.dumps({"ok": False, "error": str(error)}, ensure_ascii=True))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
