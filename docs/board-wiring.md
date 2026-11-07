# T100 wiring recovered from the earlier scanner

Source: the owner's earlier **Set up radio control and touchpad** bench session
and its `MULTI/Src/tool_radio.c` implementation.
The user reported a partly working nRF scanner; this is not complete RF qualification.

| Socket signal | STM32 pin | Peripheral/use |
|---|---|---|
| SCK | PB3 | SPI3 AF6 |
| MISO / CC SO | PB4 | SPI3 AF6; CC ready indication while selected |
| MOSI / CC SI | PB5 | SPI3 AF6 |
| CSN / CC CSn | PG7 | Active low GPIO |
| nRF CE / CC GDO0 | PG6 | nRF output; **input for CC1101** |
| nRF IRQ / CC GDO2 | PG8 | Input |
| Supply | 3.3 V | Never connect radio VCC to 5 V |

The earlier implementation also sets PB14 high as a GPIO output before opening
SPI3. Its electrical purpose is not established by that code; do not relabel it
as a radio signal without a schematic/continuity check.

The previous chat recorded removing a sideways jumper near PA9/PA10 to restore
USB. The user's current correction is that the jumpers were then **reoriented
to match the product photos**, not permanently removed. Preserve that corrected
physical arrangement. Their possible serial-interface selection role has not
been confirmed from a schematic. PA9 is the SDK UART TX pin; this implementation
disables USB VBUS sensing in software so it does not claim PA9.

The CC1101 connector image in the earlier chat mapped GDO0 to CE and GDO2 to IRQ,
but actual CC1101 detection remained unresolved. A matching logical pin name is
not proof of matching physical module orientation, supply, or SO-ready timing.
Do not assume a generic CC1101 module plugs into an nRF24 socket without the
correct adapter. No verified CC1101 register profile or detection is claimed.

`USBHAT.APP` now uses these pins for the real nRF24 driver only after explicit
selection. PB14 remains untouched. CC1101 is unsupported. See
[nRF24 bench](nrf24-bench.md) for the fixed profile, limits and test procedure.
The transport accepts binary AA/7D/7E without treating payload bytes as commands.

## Existing launcher ownership

The target remains the installed PRECHIN T100 profile marked STM32F407ZGT6,
not the similar Euse F407VET6 page. Preserve the SDK's display, calibrated touch,
SD driver, linker and handoff. The Euse page's electrical cautions are evidence
about that Euse board, not verified measurements of this T100.

USB uses PA11 D− and PA12 D+. ESP32-S3 native USB uses GPIO19 D− and GPIO20 D+,
through the board's native USB connector. COM3 is the separate CH343 UART
adapter, VID:PID 1A86:55D3. N16R8 does not identify a VBUS
power switch. The owner's working arrangement uses STM32 USB-UART connected to
PC or a power source, and STM32 USB-Slave connected to ESP32 native USB OTG.
This ESP32 board does not source OTG power. ESP32 COM is needed only for flashing
or optional UART diagnostics. This observation does not establish isolation or
backfeed protection of either board's USB power rails.
