# STM32 Launcher Dev

A local app template, command-line tool and Codex plugin for the T100 SD launcher.
The template is based on the working touch app and preserves the live screen,
touch calibration and SD handoff used by the installed launcher.

Create an app, write its feature logic, build it, then copy its checked `.APP`
package to the card. The launcher installs the package. These developer tools
create files and run the compiler; they do not flash or reset the board.

The template separates editable `Src/app.c` and `Src/app.h` from the fixed board
framework. MENU and holding WK_UP return to the launcher. KEY0 held recalibrates
and saves the shared calibration. Hardware setup and the reserved memory layout
are already provided. See [app development](docs/APP-DEVELOPMENT.md).

The Python CLI requires Python 3.12. Building requires STM32CubeIDE 2.2.0 and its
ARM toolchain. The local MCP server additionally uses `mcp==1.30.0`. This PC's
plugin uses the existing STM32 Workbench Python runtime.

```powershell
python app_factory.py create HELLO "Hello App"
python app_factory.py validate HELLO
python app_factory.py build HELLO
python app_factory.py package HELLO
```

Projects default to `Documents/Codex/STM32LauncherApps`. Set
`STM32_LAUNCHER_APPS`, or pass `--root` before the CLI subcommand, to choose
another directory. The Codex plugin has one configured managed app root and
accepts app IDs rather than arbitrary filesystem paths. App IDs have up to
eight uppercase letters, digits or underscores and start with a letter.

`validate` checks fixed board support, project configuration and memory layout.
`build` compiles a fresh copy and checks the exported ELF and binary. `package`
requires matching source and artifact digests from that successful build. The
tool reports incompatible edits or stale builds instead of generating a package.
Compatibility checks do not prove that every new app function behaves correctly.

Copy the resulting `dist/HELLO.APP` to `APPS/HELLO.APP` on the FAT32 card, preserve
the existing SYNTH calibration folder, then choose SCAN and INSTALL in the
launcher. New apps need hardware testing for their own features.

The source includes the ST/ARM/FatFs notices retained from the tested template.
Their licenses remain with the template files. This is a local developer kit
for this board profile, rather than a universal STM32 firmware converter.
