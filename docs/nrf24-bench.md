# Real nRF24 bench

Install the matching USBHAT.APP and Bruce nrf24 firmware. Radio socket stays idle
until explicitly selecting NRF24. This replaces mock loopback; CC1101 returns
unsupported. No jammer, carrier mode, mouse/keyboard injection, or other Bruce
local-radio tools are enabled by this port.

Use Others -> USB HAT Bench (or RF/NRF menu, which enters USB HAT Radio):
- NRF24 detect / start RX: two-pattern RF_CH write/read test; register-bus success
  is not proof of the antenna, RF sensitivity, module identity or supply quality.
- NRF24 energy scanner: receive-only channels 0..125, 2400..2525 MHz, one
  threshold bit per channel each sweep. It is not RSSI in dBm or packet decoding.
  Tap the upper-left back region to exit. Packet reception pauses while scanning.
- NRF24 send bench packet: one real 32-byte packet, zero-padded, not a loopback.
  TX_DS means transmitted, not delivered: auto-ACK is disabled.
- Radio off: power-down and restore app-owned pin configurations.

Peer profile: channel 76 (2476 MHz), 1 Mbps, CRC16, five-byte address sent over
SPI in order 55 53 42 48 31 (ASCII USBH1), pipe 0 only, fixed 32-byte payload,
no dynamic payload, no auto-ACK or retries, TX setting -18 dBm (external PA
modules may have additional gain). Peer writes exactly these address bytes;
avoid ambiguous integer address byte order. TX payload begins ASCII USBHAT,
AA 7E 7D 00 FF, then zero padding. STM32 forwards received 32-byte payloads
through logical radio channel 1; ESP32 prints them on its UART.

Pins: PB3 SCK, PB4 MISO, PB5 MOSI (SPI3 AF6, APB1 /32); PG7 CSN, PG6 CE,
PG8 IRQ input with pull-up. FIFO is polled each foreground update (nominal 5 ms),
not a new EXTI interrupt owner. High-rate continuous traffic can overflow the
radio's three-payload FIFO. Host/STM queues are bounded; RX does not block USB.
Hardware SPI waits have finite spin budgets. Missing/floating buses fail the
register test; no fake receive data is produced. PB14 is NOT driven because its
purpose in the earlier scanner remains unverified. Detection failure may still
require confirming module orientation, 3.3V supply and PB14's electrical role.
Do not select NRF24 with a CC1101 fitted: PG6 is CE output for NRF24 but GDO0
input for CC1101. Follow the recorded socket pinout, not module resemblance.

Calibration, display, SD and launcher SDK files are unchanged. App shutdown
powers down the radio before returning to the launcher/recalibration flow.

Specification: Nordic nRF24L01+ Product Specification v1.0, SPI commands,
register map and RPD (130us RX settling +40us AGC; this implementation samples
after >=1ms). https://devzone.nordicsemi.com/cfs-file/__key/communityserver-discussions-components-files/4/nRF24L01P_5F00_PS_5F00_v1.0.pdf

Native regression: compile tests/nrf24_test.c with Src/nrf24.c. A simulated
register/FIFO bus verifies missing-module rejection, the fixed profile, TX
bounds and padding, receive payloads, all 126 scan bins, RX restoration,
bus-failure shutdown and pin-owner close callbacks. Physical RF qualification
requires the user's board and a matching peer; it has not been claimed.
