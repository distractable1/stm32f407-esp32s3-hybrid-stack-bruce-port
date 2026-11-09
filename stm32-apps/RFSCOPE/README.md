# RF Scope diagnostic 0.3

This revision compares against the exact hardware-working Multi Tool touchpad APP
uploaded by the owner. Its SHA-256 is
578ca5e921f97740723602951fbad362ec9bfa6970d59b565617c753dc14e74f.
The uploaded APP is byte-identical to projects/multi's dist/MULTI.APP.

## Changes

- Match the working Multi Tool's PB14 output HIGH setup and SPI3 /16 prescaler.
- Preserve PB14's prior mode, type and output latch on exit/failure.
- Keep PG6/GDO0 and PG8/GDO2 as MCU inputs for CC1101.
- Match its 50 us reset CS pulse and wait for SO ready before releasing CS after SRES.
- Show the initialization stage, SPI wait failure, invalid ID or failed readback.
- Show dashes when no ID read completed, rather than default zeroes.
- Expire press highlights even if radio startup fails; KEY1 preserves startup errors.

PB14's electrical function on this T100 remains unverified. It may be an auxiliary
chip-select, but the copied setup is not evidence of its purpose. No other GPIO
assignments are changed. Pin-guard source is still not integrated.

## Socket mapping from hardware-working Multi Tool

| Module header pin | Signal | STM32 |
|---|---|---|
| 1 | GND | GND |
| 2 | VCC | 3.3 V |
| 3 | GDO0 / nRF CE | PG6 (input for CC1101) |
| 4 | CSn | PG7 |
| 5 | SCLK | PB3 AF6 SPI3 |
| 6 | SI / MOSI | PB5 AF6 SPI3 |
| 7 | SO / MISO | PB4 AF6 SPI3 |
| 8 | GDO2 / nRF IRQ | PG8 (input) |

These MCU pins are supported by the working nRF test. This does not prove the CC
module's physical connector orientation or its power and reset response.

## Testing

Back up the previous RFSCOPE.APP and install dist/RFSCOPE.APP through the existing
SD launcher. The launcher label is RF Scope Diag 03 and screen title RF Scope D03.
TX starts OFF. Tap RETRY once and photograph all status lines.

- RESET + SO HIGH: SO did not become ready while selected; no ID was read.
- IDENT + BAD CHIP ID: SPI exchanges completed, but the ID was not accepted.
- SPI TX WAIT / SPI RX WAIT / SPI BUSY: MCU peripheral transfer timed out.
- VERIFY + READBACK 10 / 0F: configuration register readback disagreed.
- RUN + SPI OK: initialization/configuration reads succeeded; physical RF still
  needs a matching receiver test.

SO:a/b reports sampled SO levels with CS high / during selected ready wait;
2 means not sampled. ST is the last command-header status byte. P14:SET records
the HIGH setup attempt, not a measured voltage; pins are restored after failure.

The new firmware was compiled and its launcher header, checksum, vectors and
hard-float ABI checked. It has not been tested on the physical board.
The existing linker/launcher flash layout assumes 1 MiB; E/G capacity identification
remains unresolved and is independent of this socket comparison.
