# Developing T100 launcher applications

This template targets the STM32F407ZGT6 board with the existing 320 × 240
ILI9341/ST7789 display, resistive touch interface and SD socket. The permanent
launcher owns the initial board setup. Applications use the same board-support
ABI and inherit its live display, touch calibration, GPIO, clocks and SD SPI
state. Installing another `.APP` changes the application slot; it does not
replace the launcher.
The existing launcher/app handoff has been confirmed working on the target
board. Each new application's own peripherals and behavior still need testing.

## Project files

The complete scaffold is based on the working `touch-launcher-app` CubeIDE
project. Retain its startup, linker script, CMSIS, FatFs, SD driver and calibration
storage sources. `board_support.c/h` alone are not a complete firmware project.
Edit `Src/app.c/h` for application features. The factory treats `main.c`, the
SDK, startup, linker and other framework files as immutable compatibility files.

| File | Responsibility |
| --- | --- |
| `launcher-app.json` | Factory metadata, including application ID and display name. |
| `Src/main.c` | Shared lifecycle, touch edges, calibration, title, MENU and footer. |
| `Src/app.c` | Your application state, UI and behavior; start editing here. |
| `Src/app.h` | Stable application callback contract and button constants. |
| `Src/board_support.c/h` | Shared, compatible hardware drivers and handoff consumer. |
| `Src/sd_diskio.c` | Inherited SD SPI state and FatFs disk access. |
| `Src/calibration_store.c/h` | Shared SD calibration records. |
| `STM32F407ZETX_FLASH.ld` | Application flash slot and reserved handoff RAM. |
| `Startup/startup_stm32f407zetx.s` | Application vectors and normal C startup. |

The factory replaces `APP_DISPLAY_NAME` in `app.c`; `app_display_name` is then
ordinary constant application metadata. The common title bar displays at most
25 characters so it stays clear of MENU. Keep the factory's JSON metadata in
sync when renaming an application.

## Lifecycle and input

`main.c` calls `board_init()`, then `app_init()`. Its foreground loop services
`board_background()`, polls buttons and touch, calls `app_update(now)`, and waits
five milliseconds. `now` is the board's wrapping 32-bit millisecond clock; use
unsigned subtraction for elapsed-time checks.

| Callback | Expected behavior |
| --- | --- |
| `app_init()` | Open app resources and repaint the client area. Called at startup and again after calibration. Preserve user state across this resume. |
| `app_update(uint32_t now)` | Perform bounded foreground work and redraw only changed UI. |
| `app_touch(int x, int y, bool pressed)` | Receive press/movement with `pressed=true`, or release at the last coordinates with `false`. |
| `app_button(unsigned buttons)` | Handle `APP_BUTTON_KEY1` and `APP_BUTTON_WK_UP`. Multiple bits can arrive together. |
| `app_shutdown()` | Sync/close all files and stop app-owned hardware before calibration or returning to the launcher. |

Identical touch coordinates are suppressed. Leaving the client area sends a
release; moving back into it begins a new contact. The initial contact that
launched the app is ignored until it is lifted. Use your own previous pressed
state to distinguish a press from movement, as the counter example does.

The common framework reserves the top 30 rows and footer:

| Area | Bounds |
| --- | --- |
| Title and MENU | `y=0..29`; MENU accepts `x=224..319`. |
| App client area | `x=0..319`, `y=30..211`. |
| Common footer | `y=212..239`. |

Hold KEY0 for calibration. The framework releases the current app touch, calls
`app_shutdown()`, runs and saves calibration, then calls `app_init()` and redraws
the common frame. Do not keep open FatFs objects across that transition.

Holding WK_UP for 1.5 seconds or touching MENU calls `app_shutdown()` and resets
into the launcher. The red RESET button also resets into the launcher, but
cannot run application cleanup. Save valuable state during normal operation.

KEY1 and WK_UP press events are debounced for approximately 60 ms. The SDK emits
the initial WK_UP press before it knows whether the button will remain held;
a long hold therefore also delivers that initial `APP_BUTTON_WK_UP` event.
Reserve consequential actions for explicit UI confirmation instead of treating
that event as an exclusive short press.

The supplied counter demonstrates separate user logic: touch `+1` or KEY1 to
increment, touch RESET or press WK_UP to clear. Its state survives calibration,
and `app_update()` only repaints the count when it changes.

## Board ownership and reserved pins

Do not repurpose these pins or reset their GPIO banks. The shared drivers use
them directly and the physical headers are already connected one to one.

| Function | Pins |
| --- | --- |
| LCD data D0..D15 | PD14, PD15, PD0, PD1, PE7..PE15, PD8, PD9, PD10, in that order. |
| LCD control | PG12 CS, PF12 RS/DC, PD5 WR, PD4 RD, PB15 backlight. |
| Touch | PB0 clock, PF11 MOSI, PB2 MISO, PC13 CS, PB1 pen interrupt input. |
| SD SPI | PC12 clock, PD2 MOSI, PC8 MISO, PC11 CS; reserve the SD socket's PC9/PC10 lines too. |
| Buttons | PE4 KEY0, PE3 KEY1, PA0 WK_UP; red button is hardware reset. |
| SDK serial logging | USART1 TX on PA9. |
| USB Slave | PA11 D− and PA12 D+; an app adding USB initializes its own USB peripheral and interrupt. |

The board profile uses the 8 MHz external crystal, a 168 MHz CPU clock, 42 MHz
APB1, 84 MHz APB2 and the 48 MHz USB clock. Keep these clock settings when using
the shared drivers. The SDK owns `SystemInit()`, `SystemCoreClock`,
`SysTick_Handler()` and `tusb_time_millis_api()`; do not define a second copy.
`SystemInit()` takes VTOR from the startup's `g_pfnVectors` symbol, so do not
hardcode a base-address vector table in application code.

## Handoff ABI and memory layout

Applications link at `0x08020000` with a maximum binary length of `0xDFFC0` bytes.
The 64 bytes at `0x080FFFC0..0x080FFFFF` belong to the launcher's installation
footer. Keep the supplied flash region length so code, constants and data load
images cannot enter that footer.

Normal SRAM is `0x20000000..0x2001FEFF`, with initial MSP at `0x2001FF00`. The top
256 bytes at `0x2001FF00..0x2001FFFF` are reserved in both projects. Do not place
variables, heap, stack or startup clearing ranges there. The version-1 handoff
uses its first 52 bytes:

| Offset | Field |
| --- | --- |
| 0, 4 | Magic and ABI version. |
| 8, 12 | Core clock and SD initialization/addressing state. |
| 16, 20 | LCD identity and rotation. |
| 24..47 | Six 32-bit floating-point calibration coefficients. |
| 48 | FNV-1a checksum of the preceding 48 bytes. |

The launcher writes the magic word last. `board_init()` snapshots, validates and
consumes the handoff. It restores software calibration/SD state and configures
the app's SysTick, while retaining the live LCD, touch, SD and RCC registers.
Cold initialization is an SDK recovery fallback for a missing or invalid
handoff; normal use starts the app through the launcher. Use
`board_was_inherited()` for diagnostics; application code should call the same
`board_init()` API in either case.

NVIC enables/pending interrupts and SysTick are cleared before transfer. Your
app owns its vector table and must initialize any additional interrupt-driven
peripheral it uses. Keep startup and linker files from the scaffold instead of
copying an unrelated vendor project's clock-reset startup.

## USB, background work and SD sharing

USB/DSP applications can provide a strong `board_background()` definition in
`app.c`. The SDK's default is weak and does nothing. This hook also runs inside
drawing and delay helpers: keep it nonblocking and guard services until their
initialization succeeds. Do not call drawing, delays, FatFs or `app_update()`
from the hook; those would recurse or reenter drivers. Keep filesystem work in
foreground code and split long work into bounded chunks.

Every app owns its own FatFs objects. No open `FIL`, directory or mounted-volume
object is transferred. The card remains configured in SPI mode, so normal
`f_mount()` uses the imported disk state instead of reinitializing the card.
The current FatFs configuration has no long filenames and is not reentrant;
use 8.3 names and serialize filesystem operations. Give your data a separate
directory, such as `0:/MYAPP/`.

Reserve `0:/APPS/` for launcher packages and `0:/SYNTH/CAL0.BIN` plus `CAL1.BIN`
for the shared calibration. Close files and unmount your volume in
`app_shutdown()` before calibration or returning. Stop or detach app-owned USB,
timers and DMA as appropriate; leave the shared board configuration intact.

## Create, build and package

From the development kit directory, the command-line workflow is:

```powershell
python app_factory.py status
python app_factory.py list
python app_factory.py create MYAPP "My App"
python app_factory.py validate MYAPP
python app_factory.py build MYAPP
python app_factory.py package MYAPP
```

Create the project, edit its `Src/app.c/h`, then validate, build and package.
Creation refuses to overwrite an existing app. The default app collection is
`Documents/Codex/STM32LauncherApps` under the current user, overridden by the
`STM32_LAUNCHER_APPS` environment variable. Put `--root APPS_ROOT` before the
subcommand to choose another collection:

```powershell
python app_factory.py --root "D:/My STM Apps" create MYAPP "My App"
```

The build writes `build/MYAPP.elf` and `build/MYAPP.bin` inside that app's
directory. Packaging writes `dist/MYAPP.APP`. A build receipt prevents packaging
an old binary after source changes: rebuild before packaging your edits.
Validation checks metadata and compatibility files; keep generated startup,
linker and shared drivers intact.

The plugin exposes the same operations as `status`, `list_apps`, `create_app`,
`validate_app`, `build_app` and `package_app`. The baseline template configures
no USB class; a USB app adds its own initialization and service hook.

For reference, the equivalent manual workflow using the original source archive is:

1. Copy the complete scaffold to a new directory, update its CubeIDE project
   name and application metadata, replace the display-name token, and edit
   `Src/app.c/h`. Keep the hardware framework unchanged.
2. Import the existing project into STM32CubeIDE and build its Debug configuration.
   The ELF must use the supplied application linker script.
3. Export a raw binary with the installed toolchain's
   `arm-none-eabi-objcopy -O binary input.elf output.bin`.
4. Package the binary with the existing launcher packager. From the original
   launcher source archive, for example:

   ```powershell
   python tools/package_launcher.py --package-only --app path/to/new-app.bin --name "My App" --filename MYAPP.APP --sd-root sd-files
   ```

5. Copy the resulting `APPS/MYAPP.APP` onto the FAT32 SD card using a card reader.
   Eject the card cleanly, insert it into the board, select SCAN, then INSTALL.
   Keep power connected until verification and installation finish.

The package filename must be 8.3 with an `.APP` extension. Package metadata and
image checks reject incompatible addresses, vectors and sizes. Build/package
operations do not require programming the launcher again; do not use whole-chip
erase or flash an application binary at `0x08000000`.

Before handing an app to someone else, build it, check its maximum stack and
memory usage, validate its package, and test MENU, held WK_UP, calibration
resume and SD cleanup. Hardware checks require a real board; a successful
compile alone does not verify wiring or peripheral behavior.

## Confirmed USB jumper setting

On this board, leave the PA9�PA10 bridge jumper open. The user confirmed that
removing it restored USB connectivity after repeated enumeration failures.
Keep this setting separate from the BT0/BT1 boot selectors. The electrical
mechanism has not been established; this records the tested configuration.
