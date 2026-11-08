# T100 launcher board support

The launcher initializes the ILI9341 display, touch interface and SD slot. It
loads the existing SD calibration once. An app built for this launcher calls
`board_init()`, which adopts the live board state when a valid handoff is present.
It sets up its own SysTick but skips clock, LCD, GPIO and SD-card initialization.
The app can then add its own peripherals, such as USB audio and MIDI.

Use `touch-launcher-app` as the minimal complete CubeIDE template. It includes
this board-support source, the CMSIS headers, calibration storage, FatFs and the
SD disk driver. `board_support.c/h` alone are not a complete project.

Apps link their vector table at 0x08020000 and reserve 64 bytes at the end of the
1 MiB flash map for the installation footer. The top 256 bytes of SRAM, starting
at 0x2001ff00, are reserved for the handoff. Both the launcher and apps set the
initial stack to 0x2001ff00. Never place app variables or stack in that area.

The version-1 handoff contains a magic value, ABI version, core clock, SD SPI
addressing/initialization state, detected LCD identity, rotation, six calibration
coefficients and an FNV checksum. The launcher commits the magic word last.
The app snapshots and validates it, then clears the magic. Driver configuration
must remain compatible with this version of board support.

Only driver state is transferred. FatFs handles are closed before a switch and
each app owns its filesystem objects. The SD card stays configured in SPI mode.
Clocks and GPIO are preserved, while SysTick and NVIC interrupt state are cleared
for the new app. Every app must install its own vector table and interrupt owners.

`board_touch()` returns calibrated screen coordinates. `rectangle()`, `text()`
and `panel_button()` draw on the inherited screen. `physical_buttons()` returns
KEY0-held, KEY1-pressed, WK_UP-pressed and WK_UP-held as bits 0 through 3. The
long WK_UP event arrives after 1.5 seconds. `board_recalibrate()` updates the
shared SD calibration. `board_return_to_launcher()` resets into the base menu.
Apps should close files and stop their own active hardware before returning.

`board_background()` is a weak foreground hook. Audio apps override it to keep
their USB and DSP work serviced while drawing or waiting. It must not recursively
call board drawing or filesystem operations. Filesystem operations belong in
foreground code. Display/touch/SD pins are reserved by this board profile.

The current synth app still has the original half-second RAM sample buffer.
SD audio streaming is separate work and is not included in this launcher build.

To add another app, copy the touch-app CubeIDE project and retain its board
support, linker layout and startup. Replace its main feature loop and build it.
Export the ELF as a raw binary using arm-none-eabi-objcopy. The source archive's
Python packaging tool can produce a standalone SD package without reflashing
the launcher. For example, from the extracted source archive directory:

```powershell
python tools/package_launcher.py --package-only --app path/to/new-app.bin --name "My App" --filename MYAPP.APP --sd-root sd-files
```

Copy `sd-files/APPS/MYAPP.APP` onto the card. The filename must use the 8.3 form.
The tool rejects images with incompatible addresses, vectors or size. A new
feature still needs code and a compatible app build; SD space supplies the
library capacity rather than additional CPU speed or working RAM.
