"""Package a T100 launcher and its relocated STM32 synth application."""

import argparse
from pathlib import Path
import re
import struct
import zlib


FLASH_BASE = 0x08000000
FLASH_SIZE = 0x100000
LAUNCHER_LIMIT = 0x20000
APP_BASE = FLASH_BASE + LAUNCHER_LIMIT
FOOTER_OFFSET = 0xFFFC0
APP_LIMIT = FOOTER_OFFSET - LAUNCHER_LIMIT
HEADER_SIZE = 64
MAGIC = 0x31505041
VERSION = 1
BOARD = 0x54313030
COMMIT = 0x434F4D54
HEADER = struct.Struct("<6I32sII")


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def validate_image(image, load_address, maximum_size, label):
    """Check an image's size, initial stack, and Thumb reset vector."""
    if not 8 <= len(image) <= maximum_size:
        raise ValueError(f"{label} length must be between 8 and {maximum_size} bytes")

    stack, reset = struct.unpack_from("<II", image)
    in_sram = 0x20000000 < stack <= 0x2001FF00
    in_ccm = 0x10000000 < stack <= 0x10010000
    if stack % 8 or not (in_sram or in_ccm):
        raise ValueError(f"{label} initial stack must be 8-byte aligned in SRAM or CCM")
    if not reset & 1:
        raise ValueError(f"{label} reset vector must select Thumb state")
    entry = reset & ~1
    if not load_address + 8 <= entry < load_address + len(image):
        raise ValueError(f"{label} reset vector is outside its image at 0x{load_address:08X}")


def make_header(application, name="USB Synth"):
    validate_image(application, APP_BASE, APP_LIMIT, "Application")
    try:
        encoded_name = name.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError("Application name must use printable ASCII") from error
    if not 1 <= len(encoded_name) <= 31 or any(c < 32 or c > 126 for c in encoded_name):
        raise ValueError("Application name must contain 1 to 31 printable ASCII characters")

    prefix = struct.pack(
        "<6I32s", MAGIC, VERSION, BOARD, APP_BASE, len(application),
        crc32(application), encoded_name.ljust(32, b"\0"),
    )
    return prefix + struct.pack("<II", crc32(prefix), COMMIT)


def package_app(application, name="USB Synth"):
    return make_header(application, name) + application


def validate_package(package):
    """Validate the complete APP file and return its raw application image."""
    if len(package) < HEADER_SIZE:
        raise ValueError("APP file is shorter than its 64-byte header")
    magic, version, board, load, length, expected_crc, name, header_crc, commit = HEADER.unpack_from(package)
    if magic != MAGIC or version != VERSION or board != BOARD or load != APP_BASE:
        raise ValueError("APP header has an unsupported magic, version, board, or load address")
    if commit != COMMIT:
        raise ValueError("APP header is not committed")
    if header_crc != crc32(package[:56]):
        raise ValueError("APP header CRC does not match")
    if not 8 <= length <= APP_LIMIT:
        raise ValueError("APP header length is outside the application region")
    if len(package) != HEADER_SIZE + length:
        raise ValueError("APP file length does not match its header")
    if b"\0" not in name:
        raise ValueError("APP name is not NUL terminated")
    display_name, padding = name.split(b"\0", 1)
    if not display_name or any(c < 32 or c > 126 for c in display_name) or any(padding):
        raise ValueError("APP name contains invalid characters or padding")

    application = package[HEADER_SIZE:]
    if expected_crc != crc32(application):
        raise ValueError("APP payload CRC does not match")
    validate_image(application, APP_BASE, APP_LIMIT, "Application")
    return application


def combine_images(launcher, application, name="USB Synth"):
    validate_image(launcher, FLASH_BASE, LAUNCHER_LIMIT, "Launcher")
    package = package_app(application, name)
    validate_package(package)
    combined = bytearray(b"\xFF" * FLASH_SIZE)
    combined[:len(launcher)] = launcher
    app_end = LAUNCHER_LIMIT + len(application)
    combined[LAUNCHER_LIMIT:app_end] = application
    combined[FOOTER_OFFSET:] = package[:HEADER_SIZE]
    return bytes(combined)


def validate_combined(combined, launcher_length):
    """Check both image regions, erased padding, and the committed footer."""
    if len(combined) != FLASH_SIZE:
        raise ValueError("Combined flash image must contain exactly 1 MiB")
    if not 8 <= launcher_length <= LAUNCHER_LIMIT:
        raise ValueError("Launcher length is outside its reserved region")
    validate_image(combined[:launcher_length], FLASH_BASE, LAUNCHER_LIMIT, "Launcher")
    footer = combined[FOOTER_OFFSET:]
    length = struct.unpack_from("<I", footer, 16)[0]
    if not 8 <= length <= APP_LIMIT:
        raise ValueError("Combined footer length is outside the application region")
    app_end = LAUNCHER_LIMIT + length
    validate_package(footer + combined[LAUNCHER_LIMIT:app_end])
    if any(byte != 0xFF for byte in combined[launcher_length:LAUNCHER_LIMIT]):
        raise ValueError("Launcher padding contains programmed bytes")
    if any(byte != 0xFF for byte in combined[app_end:FOOTER_OFFSET]):
        raise ValueError("Application padding contains programmed bytes")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--launcher", type=Path, default=Path("outputs/stm32-sd-launcher.bin"))
    parser.add_argument("--app", type=Path, default=Path("outputs/stm32-synth-launcher-app.bin"))
    parser.add_argument("--name", default="USB Synth")
    parser.add_argument("--filename", default="SYNTH.APP", help="8.3 APP filename in the APPS folder")
    parser.add_argument("--package-only", action="store_true", help="Package an app without creating an initial combined flash image")
    parser.add_argument("--sd-root", type=Path, default=Path("outputs/launcher-sd"))
    parser.add_argument("--combined", type=Path, default=Path("outputs/stm32-launcher-with-synth.bin"))
    args = parser.parse_args()

    if not re.fullmatch(r"[A-Za-z0-9_-]{1,8}\.[Aa][Pp][Pp]", args.filename):
        parser.error("--filename must be an 8.3 filename ending in .APP")
    application = args.app.read_bytes()
    package = package_app(application, args.name)
    validate_package(package)
    combined = None
    if not args.package_only:
        launcher = args.launcher.read_bytes()
        combined = combine_images(launcher, application, args.name)
        validate_combined(combined, len(launcher))

    app_path = args.sd_root / "APPS" / args.filename
    app_path.parent.mkdir(parents=True, exist_ok=True)
    app_path.write_bytes(package)
    print(f"APP package validated: {app_path} ({len(application)} payload bytes)")
    if combined is not None:
        args.combined.parent.mkdir(parents=True, exist_ok=True)
        args.combined.write_bytes(combined)
        print(f"Combined flash image validated: {args.combined} ({len(combined)} bytes)")


if __name__ == "__main__":
    main()
