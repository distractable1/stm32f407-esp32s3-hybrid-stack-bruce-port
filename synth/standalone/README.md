# STM32 USB Synth

Port for the PRECHIN T100 board marked STM32F407ZGT6, with a 2.8-inch ILI9341
touchscreen. The synth runs on the STM32 and exchanges mono 16-bit 48 kHz USB
audio plus USB MIDI with a host. The voice engine is adapted from
[k-omura/STM32F4-Digital-Synthesizer](https://github.com/k-omura/STM32F4-Digital-Synthesizer).
Dependency licenses are in `Licenses` and the bundled source headers.

Import this directory as an existing project in STM32CubeIDE. Select Debug and
build the project named STM32 USB Synth. The tested toolchain is CubeIDE 2.2.0.
The MCU configuration is STM32F407ZG with 1 MiB internal flash. The inherited
linker and startup filenames contain ZE, but the linker memory size and device
defines are configured for the ZG target.

Disconnect both USB cables before moving boot jumpers. Normal operation uses
BT0 at GND and the BT1 jumper removed. Connect USB-Slave for audio and MIDI.
USB-UART provides 115200 baud debug output. USB DFU uses BT0 at 3.3 V and BT1
at GND. The board's ROM DFU identity reports a different MCU family than the
physical marking. Preserve the factory backup when programming it.

The display is oriented with the antenna pointing up. Touch calibration is
loaded from two validated files in the SD card's SYNTH folder. The card must be
FAT32 and present at boot. A missing or invalid calibration starts the visual
calibration sequence. A failed save retains the calibration for the session.

The keyboard page provides four waveforms, volume, a 440 Hz test tone, USB
recording and sample playback. FX opens cutoff, resonance, attack, release,
vibrato and tremolo controls. MIDI CC 74, 71 and 1 control cutoff, resonance
and vibrato. The engine supports ten pitched voices.

Physical button assignments follow the schematic and need a board check.

- Hold KEY0 / PE4 about one second to recalibrate and save.
- Press KEY1 / PE3 to play the recorded sample.
- Press WK_UP / PA0 to switch keyboard and effects pages.

The sample buffer holds 24,000 mono samples, or half a second, in RAM. Start
recording on the board before sending audio to its USB playback endpoint.
PLAY SAMPLE plays it through the USB capture endpoint. Samples are lost at
power-off. SD sample storage is not implemented. USB mass storage is disabled.

On Windows, the companion app routes the board's audio input to selected PC
speakers or headphones and can send a WAV file to the USB sample input. Windows
Listen to this device also works for monitoring. Use one monitor at a time to
avoid echo. Android requires a USB audio routing app. An Android companion APK
is not included.

The current firmware builds with zero errors and warnings, passes USB
descriptor checks, and has been programmed with read-back verification.
Basic USB audio and SD calibration loading worked on this board in preceding
builds. The current engine and button behavior still need hardware checks.
