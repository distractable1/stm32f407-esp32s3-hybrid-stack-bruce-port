"""Factory regression tests; never invoke CubeIDE, a compiler, or hardware.

Run with ``python -m unittest discover -s tests -v`` from this bundle.
The project fixtures are copies of the shipped template, rather than reduced
stand-ins, so changes to its linker script and Eclipse metadata are covered.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import xml.etree.ElementTree as ET


BUNDLE = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(BUNDLE))
import app_factory as factory_module  # noqa: E402
from app_factory import AppFactory, FactoryError  # noqa: E402


APP_BASE = 0x08020000
APP_FOOTER = 0x080FFFC0
RAM_LIMIT = 0x2001FF00


def synthetic_elf(*, physical=APP_BASE, virtual=APP_BASE, machine=40,
                  stack=RAM_LIMIT, reset=None, memory_size=None,
                  extra_segments=()):
    """Return a small ELF32/ARM executable and its corresponding raw image.

    Its first eight bytes are the vectors, and its next instruction is a Thumb
    NOP. Program and section headers describe the same 32-byte flash image.
    Additional PT_LOAD entries let tests exercise zero-filled RAM bounds.
    """
    if reset is None:
        reset = APP_BASE + 9
    binary = struct.pack("<II", stack, reset) + b"\x00\xbf" * 12
    if memory_size is None:
        memory_size = len(binary)
    segments = [(1, 0x100, virtual, physical, len(binary), memory_size, 5, 4)]
    segments.extend(extra_segments)
    ident = b"\x7fELF\x01\x01\x01" + b"\0" * 9
    header = struct.pack(
        "<16sHHIIIIIHHHHHH", ident, 2, machine, 1, reset,
        52, 0x300, 0x05000400, 52, 32, len(segments), 40, 6, 3,
    )
    image = bytearray(0x300 + 6 * 40)
    image[:len(header)] = header
    for index, segment in enumerate(segments):
        struct.pack_into("<8I", image, 52 + index * 32, *segment)
    image[0x100:0x100 + len(binary)] = binary
    names = b"\0.isr_vector\0.text\0.shstrtab\0.symtab\0.strtab\0"
    image[0x140:0x140 + len(names)] = names
    symbol_names = b"\0g_pfnVectors\0_estack\0_ebss\0Reset_Handler\0"
    image[0x200:0x200 + len(symbol_names)] = symbol_names
    symbols = [
        (0, 0, 0, 0, 0, 0),
        (symbol_names.index(b"g_pfnVectors"), APP_BASE, 8, 0x11, 0, 1),
        (symbol_names.index(b"_estack"), RAM_LIMIT, 0, 0x10, 0, 0xFFF1),
        (symbol_names.index(b"_ebss"), 0x20000000, 0, 0x10, 0, 0xFFF1),
        (symbol_names.index(b"Reset_Handler"), reset, 2, 0x12, 0, 2),
    ]
    for index, symbol in enumerate(symbols):
        struct.pack_into("<IIIBBH", image, 0x180 + index * 16, *symbol)
    sections = [
        (0,) * 10,
        (names.index(b".isr_vector"), 1, 2, APP_BASE, 0x100, 8, 0, 0, 4, 0),
        (names.index(b".text"), 1, 6, APP_BASE + 8, 0x108, 24, 0, 0, 2, 0),
        (names.index(b".shstrtab"), 3, 0, 0, 0x140, len(names), 0, 0, 1, 0),
        (names.index(b".symtab"), 2, 0, 0, 0x180, len(symbols) * 16, 5, 1, 4, 16),
        (names.index(b".strtab"), 3, 0, 0, 0x200, len(symbol_names), 0, 0, 1, 0),
    ]
    for index, section in enumerate(sections):
        struct.pack_into("<10I", image, 0x300 + index * 40, *section)
    return bytes(image), binary


class ElfVerificationTests(unittest.TestCase):
    def test_accepts_arm_image_at_application_base(self):
        elf, binary = synthetic_elf()
        self.assertIsInstance(factory_module._verify_elf(elf, binary), dict)

    def test_rejects_malformed_or_wrong_architecture_elf(self):
        elf, binary = synthetic_elf()
        wrong_class = bytearray(elf)
        wrong_class[4] = 2
        wrong_endian = bytearray(elf)
        wrong_endian[5] = 2
        wrong_machine, _ = synthetic_elf(machine=3)
        for malformed in (b"", b"not an ELF", elf[:51],
                          bytes(wrong_class), bytes(wrong_endian), wrong_machine):
            with self.subTest(prefix=malformed[:20]):
                with self.assertRaises(FactoryError):
                    factory_module._verify_elf(malformed, binary)

    def test_rejects_loads_in_launcher_or_footer(self):
        for address in (0x08000000, APP_BASE - 4, APP_FOOTER - 4, APP_FOOTER):
            with self.subTest(address=hex(address)):
                elf, binary = synthetic_elf(physical=address)
                with self.assertRaises(FactoryError):
                    factory_module._verify_elf(elf, binary)

    def test_rejects_ram_segments_overlapping_handoff_or_past_ccm(self):
        for virtual in (RAM_LIMIT - 8, 0x1000FFF8):
            segment = (1, 0x120, virtual, APP_BASE + 32, 0, 16, 6, 4)
            with self.subTest(address=hex(virtual)):
                elf, binary = synthetic_elf(extra_segments=(segment,))
                with self.assertRaises(FactoryError):
                    factory_module._verify_elf(elf, binary)

    def test_rejects_invalid_vectors(self):
        for changes in ({"stack": 0x20020000}, {"stack": RAM_LIMIT - 1},
                        {"reset": APP_BASE + 8}, {"reset": APP_BASE + 33}):
            with self.subTest(changes=changes):
                elf, binary = synthetic_elf(**changes)
                with self.assertRaises(FactoryError):
                    factory_module._verify_elf(elf, binary)


class FactoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.template_temp = tempfile.TemporaryDirectory(prefix="launcher-template-test-")
        cls.addClassCleanup(cls.template_temp.cleanup)
        cls.template = Path(cls.template_temp.name) / "template"
        bundled_template = BUNDLE / "template"
        if not (bundled_template / "Src" / "app.c").is_file():
            raise AssertionError("The bundled application template is incomplete")
        shutil.copytree(bundled_template, cls.template)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="launcher-factory-test-")
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.apps = self.work / "apps"
        self.factory = AppFactory(self.apps, template_root=self.template)
        # Even an unexpected code path cannot launch an installed tool.
        self.run_guard = mock.patch.object(
            AppFactory, "_run",
            side_effect=AssertionError("Tests must not invoke external build tools"),
        )
        self.run_mock = self.run_guard.start()
        self.addCleanup(self.run_guard.stop)
        self.tools_patch = mock.patch.object(
            AppFactory, "_tools",
            return_value={"ide": Path("mock-cubeide"), "objcopy": Path("mock-objcopy")},
        )
        self.tools_patch.start()
        self.addCleanup(self.tools_patch.stop)

    def create(self, app_id="DEMO", name="Demo App"):
        result = self.factory.create_app(app_id, name)
        self.assertIsInstance(result, dict)
        projects = list((self.apps / app_id).rglob(".project"))
        self.assertEqual(len(projects), 1, "A new app must contain exactly one project")
        return projects[0].parent

    def assert_invalid(self, app_id="DEMO"):
        result = self.factory.validate_app(app_id)
        self.assertIsInstance(result, dict)
        self.assertFalse(result["ok"], result)
        self.assertTrue(result["errors"], result)
        return result

    def fake_build(self, app_id="DEMO"):
        """Supply build products at the process boundary, without a toolchain."""
        elf, binary = synthetic_elf()

        def run(args, cwd, timeout=600):
            args = [str(arg) for arg in args]
            if args[0] == "mock-objcopy":
                Path(args[-1]).write_bytes(binary)
            else:
                # CubeIDE imports a fresh staging project. Find the imported
                # project argument rather than writing into the user's tree.
                imported = Path(args[args.index("-import") + 1])
                name = ET.parse(imported / ".project").findtext("name")
                output = imported / "Debug"
                output.mkdir(parents=True, exist_ok=True)
                (output / (name + ".elf")).write_bytes(elf)
            return subprocess.CompletedProcess(args, 0, "mock build succeeded", "")

        with mock.patch.object(AppFactory, "_run", side_effect=run):
            result = self.factory.build_app(app_id, "Debug")
        self.assertIsInstance(result, dict)
        return result, elf, binary

    def test_real_template_creates_valid_project(self):
        project = self.create()
        self.assertTrue((project / "Src" / "app.c").is_file())
        self.assertTrue((project / "Src" / "main.c").is_file())
        self.assertTrue((project / "STM32F407ZETX_FLASH.ld").is_file())
        self.assertEqual(ET.parse(project / ".project").findtext("name"), "STM32 DEMO")
        ET.parse(project / ".cproject")
        result = self.factory.validate_app("DEMO")
        self.assertTrue(result["ok"], result)
        self.assertEqual(result["errors"], [])
        self.run_mock.assert_not_called()

    def test_name_is_escaped_as_c_text(self):
        name = 'Meter "A" \\ B & C'
        project = self.create(name=name)
        source = (project / "Src" / "app.c").read_text(encoding="utf-8")
        self.assertIn(json.dumps(name, ensure_ascii=True), source)
        self.assertTrue(self.factory.validate_app("DEMO")["ok"])

    def test_duplicate_app_never_overwrites_existing_sources(self):
        project = self.create()
        source = project / "Src" / "app.c"
        source.write_text(source.read_text(encoding="utf-8") + "\n/* user edit */\n",
                          encoding="utf-8")
        before = source.read_bytes()
        with self.assertRaises(FactoryError):
            self.factory.create_app("DEMO", "Replacement")
        self.assertEqual(source.read_bytes(), before)

    def test_existing_non_app_directory_is_not_overwritten(self):
        existing = self.apps / "DEMO"
        existing.mkdir(parents=True)
        sentinel = existing / "keep.txt"
        sentinel.write_text("keep me", encoding="utf-8")
        with self.assertRaises(FactoryError):
            self.factory.create_app("DEMO", "Replacement")
        self.assertEqual(sentinel.read_text(encoding="utf-8"), "keep me")
        self.assertEqual(list(existing.iterdir()), [sentinel])

    def test_rejects_invalid_ids_and_path_traversal(self):
        invalid = ("", ".", "..", "../ESCAPE", "..\\ESCAPE", "/ABS", "C:\\ABS",
                   "NINECHARS", "WITH SPACE", "A.B", "A/B", "A\\B", "A\0B")
        for app_id in invalid:
            with self.subTest(app_id=repr(app_id)):
                with self.assertRaises(FactoryError):
                    self.factory.create_app(app_id, "Demo")
        self.assertFalse((self.work / "ESCAPE").exists())
        self.run_mock.assert_not_called()

    def test_rejects_invalid_display_names(self):
        for name in ("", "X" * 32, "line\nbreak", "tab\there", "nul\0byte", "caf\u00e9"):
            with self.subTest(name=repr(name)):
                with self.assertRaises(FactoryError):
                    self.factory.create_app("DEMO", name)
                self.assertFalse((self.apps / "DEMO").exists())

    def test_status_and_list_include_created_app(self):
        self.create()
        for response in (self.factory.status(), self.factory.list_apps()):
            self.assertIsInstance(response, dict)
        self.assertIn("DEMO", json.dumps(self.factory.list_apps(), default=str))
        self.run_mock.assert_not_called()

    def test_missing_app_validation_returns_errors(self):
        self.assert_invalid("MISSING")

    def test_rejects_broken_project_xml(self):
        project = self.create()
        for filename in (".project", ".cproject"):
            target = project / filename
            original = target.read_bytes()
            with self.subTest(filename=filename):
                try:
                    target.write_text("<broken", encoding="utf-8")
                    self.assert_invalid()
                    with self.assertRaises(FactoryError):
                        self.factory.build_app("DEMO", "Debug")
                finally:
                    target.write_bytes(original)
        self.run_mock.assert_not_called()

    def test_rejects_unsafe_linker_memory_layout(self):
        project = self.create()
        linker = project / "STM32F407ZETX_FLASH.ld"
        original = linker.read_text(encoding="utf-8")
        mutations = (
            ("0x8020000", "0x8000000"),
            ("0xdffc0", "0xe0000"),
            ("0x1ff00", "0x20000"),
        )
        for before, after in mutations:
            self.assertIn(before, original)
            with self.subTest(mutation=(before, after)):
                try:
                    linker.write_text(original.replace(before, after), encoding="utf-8")
                    self.assert_invalid()
                finally:
                    linker.write_text(original, encoding="utf-8")

    def test_sdk_mutation_is_rejected_but_app_source_is_editable(self):
        project = self.create()
        source = project / "Src" / "app.c"
        source.write_text(source.read_text(encoding="utf-8") + "\n/* editable */\n",
                          encoding="utf-8")
        result = self.factory.validate_app("DEMO")
        self.assertTrue(result["ok"], result)
        sdk = project / "Src" / "main.c"
        sdk.write_text(sdk.read_text(encoding="utf-8") + "\n/* changed SDK */\n",
                       encoding="utf-8")
        self.assert_invalid()

    def test_source_hashes_change_for_edits_additions_and_deletions(self):
        project = self.create()
        initial = factory_module._source_hashes(project)
        self.assertIsInstance(initial, dict)
        self.assertTrue(initial)
        source = project / "Src" / "app.c"
        source.write_text(source.read_text(encoding="utf-8") + "\n/* edited */\n",
                          encoding="utf-8")
        edited = factory_module._source_hashes(project)
        self.assertNotEqual(initial, edited)
        added = project / "Src" / "extra.c"
        added.write_text("void extra(void) {}\n", encoding="utf-8")
        with_added = factory_module._source_hashes(project)
        self.assertNotEqual(edited, with_added)
        added.unlink()
        self.assertEqual(factory_module._source_hashes(project), edited)

    def test_build_uses_fresh_staging_and_records_exact_products(self):
        project = self.create()
        stale = project / "Debug" / "DEMO.elf"
        stale.parent.mkdir()
        stale.write_bytes(b"stale product must not be reused")
        _, elf, binary = self.fake_build()
        output = self.apps / "DEMO" / "build"
        receipt = json.loads((output / "receipt.json").read_text(encoding="utf-8"))
        self.assertEqual(receipt["schema"], 1)
        self.assertEqual(receipt["app_id"], "DEMO")
        self.assertEqual(receipt["configuration"], "Debug")
        self.assertEqual(receipt["sources"], factory_module._source_hashes(project))
        self.assertEqual(receipt["elf"], "build/DEMO.elf")
        self.assertEqual(receipt["binary"], "build/DEMO.bin")
        self.assertEqual(receipt["elf_sha256"], hashlib.sha256(elf).hexdigest())
        self.assertEqual(receipt["bin_sha256"], hashlib.sha256(binary).hexdigest())
        self.assertEqual((output / "DEMO.elf").read_bytes(), elf)
        self.assertEqual((output / "DEMO.bin").read_bytes(), binary)
        self.assertEqual(stale.read_bytes(), b"stale product must not be reused")

    def test_build_failure_cannot_reuse_existing_products(self):
        project = self.create()
        self.fake_build()
        source = project / "Src" / "app.c"
        source.write_text(source.read_text(encoding="utf-8") + "\n/* new revision */\n",
                          encoding="utf-8")
        failed = subprocess.CompletedProcess(["mock-cubeide"], 1, "", "compile failed")
        with mock.patch.object(AppFactory, "_run", return_value=failed):
            with self.assertRaises(FactoryError):
                self.factory.build_app("DEMO", "Debug")
        with self.assertRaises(FactoryError):
            self.factory.package_app("DEMO")

    def test_release_request_invalidates_previous_debug_products(self):
        self.create()
        self.fake_build()
        self.factory.package_app("DEMO")
        app = self.apps / "DEMO"
        products = [app / "build" / name for name in
                    ("receipt.json", "DEMO.elf", "DEMO.bin")]
        products.append(app / "dist" / "DEMO.APP")
        self.assertTrue(all(path.is_file() for path in products))
        with self.assertRaises(FactoryError):
            self.factory.build_app("DEMO", "Release")
        self.assertFalse(any(path.exists() for path in products))
        self.run_mock.assert_not_called()

    def test_package_requires_build_receipt(self):
        self.create()
        with self.assertRaises(FactoryError):
            self.factory.package_app("DEMO")
        self.run_mock.assert_not_called()

    def test_package_is_header_plus_verified_binary(self):
        self.create()
        _, _, binary = self.fake_build()
        result = self.factory.package_app("DEMO")
        self.assertIsInstance(result, dict)
        packages = list((self.apps / "DEMO").rglob("*.APP"))
        self.assertEqual(len(packages), 1)
        import package_launcher
        package = packages[0].read_bytes()
        self.assertEqual(len(package), 64 + len(binary))
        self.assertEqual(package_launcher.validate_package(package), binary)
        self.run_mock.assert_not_called()

    def test_source_edits_invalidate_packaging_receipt(self):
        project = self.create()
        self.fake_build()
        source = project / "Src" / "app.c"
        source.write_text(source.read_text(encoding="utf-8") + "\n/* after build */\n",
                          encoding="utf-8")
        with self.assertRaises(FactoryError):
            self.factory.package_app("DEMO")

    def test_artifact_tampering_invalidates_packaging_receipt(self):
        self.create()
        self.fake_build()
        for extension in ("elf", "bin"):
            artifact = self.apps / "DEMO" / "build" / ("DEMO." + extension)
            original = artifact.read_bytes()
            with self.subTest(extension=extension):
                try:
                    artifact.write_bytes(original + b"tampered")
                    with self.assertRaises(FactoryError):
                        self.factory.package_app("DEMO")
                finally:
                    artifact.write_bytes(original)

    def test_symlinked_source_cannot_escape_project(self):
        project = self.create()
        source = project / "Src" / "app.c"
        original = source.read_bytes()
        outside = self.work / "outside.c"
        outside.write_bytes(original)
        source.unlink()
        link = source
        try:
            try:
                os.symlink(outside, source)
            except (OSError, NotImplementedError) as error:
                source.write_bytes(original)
                # Windows junctions do not require the symbolic-link privilege.
                # Use the stdlib Windows helper rather than starting a shell.
                if os.name != "nt":
                    self.skipTest("Creating symlinks is unavailable: " + str(error))
                try:
                    import _winapi
                    create_junction = _winapi.CreateJunction
                except (ImportError, AttributeError):
                    self.skipTest("Creating symlinks/junctions is unavailable: " + str(error))
                target = self.work / "outside-source"
                target.mkdir()
                (target / "outside.c").write_bytes(original)
                link = project / "Src" / "external-source"
                try:
                    create_junction(str(target), str(link))
                except OSError as junction_error:
                    self.skipTest("Creating junctions is unavailable: " + str(junction_error))
            self.assert_invalid()
            with self.assertRaises(FactoryError):
                self.factory.build_app("DEMO", "Debug")
            self.assertEqual(outside.read_bytes(), original)
        finally:
            if link.is_symlink():
                link.unlink()
            elif link != source and link.exists():
                # rmdir removes this junction itself, never its target tree.
                link.rmdir()
            if not source.exists():
                source.write_bytes(original)
        self.run_mock.assert_not_called()


if __name__ == "__main__":
    unittest.main()
