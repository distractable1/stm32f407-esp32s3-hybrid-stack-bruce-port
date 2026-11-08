---
name: launcher-apps
description: Create, build, validate, and package apps for the user's STM32 T100 SD launcher with inherited touchscreen calibration and SD interface state.
---

Use this workflow when making or modifying an app for the T100 SD launcher.
Use the local STM32 Launcher Dev tools. Read status first and list_apps when
selecting an existing app. Keep the existing STM32 Workbench plugin for separate
hardware diagnosis or authorized programming.

For a new feature, use create_app with a unique 1–8 character uppercase app ID
and a display name of up to 31 printable ASCII characters. The tool creates a
complete CubeIDE project. It never replaces an existing app. Implement feature
logic in the generated Src/app.c and Src/app.h, adding separate feature modules
when useful. Read the [development guide](../../docs/APP-DEVELOPMENT.md) and the
generated app.c before editing.

The framework supplies board initialization/adoption, calibrated touch, the
shared header and MENU control, recalibration, and return to the launcher. Keep
the protected main.c, board-support files, startup, linker map and project
configuration unchanged during ordinary feature development. A tool rejection
identifies a compatibility issue to fix, not a check to bypass. Deliberate SDK
changes need a coordinated template/launcher version and their own review.

The installed target is the PRECHIN T100 marked STM32F407ZGT6, with a 2.8-inch
ILI9341, 168 MHz core and FAT32 SD. App flash starts at 0x08020000 and has
0xdffc0 usable bytes; the final 64 bytes of the 1 MiB map hold installation
metadata. SRAM ends at 0x2001ff00 for apps; the upper 256 bytes are reserved for
the live version-1 board handoff. The factory checks actual ELF load segments,
vectors, startup and protected SDK hashes. An arbitrary BIN cannot be relocated
by renaming it.

Launcher-to-app transfers preserve clocks, LCD/touch GPIO, calibrated transform
and SD SPI driver state. Apps own their vector table, SysTick, extra peripherals
and filesystem objects. Do not reset or reassign the existing LCD/touch/SD pins.
Use app_shutdown to close files and stop app-owned hardware before recalibration
or return. Recalibration remounts FatFs, then app_init is called to resume/repaint.
The long WK_UP action and on-screen MENU always return through the framework.

Use validate_app, then build_app, inspect diagnostics, then package_app. Build
and package operations do not flash, reset or enumerate hardware. Package
creation requires a fresh successful build receipt and matching source/ELF/BIN
digests. Any source edit requires another build before packaging. Do not package
old artifacts after a failed build. Deliver the resulting dist/APPID.APP with
instructions to copy it to SD:/APPS and use SCAN then INSTALL in the launcher.

Keep implementation work inside the configured app directory. Do not silently
overwrite another app, alter the launcher, flash firmware, format the card, or
replace the user's SD calibration. Those are separate actions with separate
authorization requirements. Existing session authorization still applies.

The launcher and synth launch/return path were tested on the board. The original
synth recorder remains a half-second RAM buffer. SD audio streaming and Android
companion software are separate features and must not be claimed implemented.
