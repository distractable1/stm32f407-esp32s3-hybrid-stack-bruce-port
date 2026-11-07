# ESP32-S3 / STM32 T100 USB HAT bench

Current bench status: touch works according to the owner's test, but the display
has shown a single dot. The diagnostic revision logs every tile on both MCUs
and keeps LCD chip-select asserted across each full window write. Software
tests verify the complete area; a hardware retest is still required before
calling the display issue resolved.

One USB CDC ACM function carries three framed logical channels. The ESP32 is
USB host; the STM32 is USB device. No external hub or inter-board GPIO wiring
is used. Native USB is exclusively owned by the host stack on ESP32; console
output uses the board's UART/CH343 connection (currently COM3, 115200 baud).

## STM32 SD app

The built app is `stm32-apps/USBHAT/dist/USBHAT.APP`. Copy it to `SD:/APPS`,
safely eject, insert into the T100, then SCAN, select USB Peripheral Bench,
INSTALL and RUN using the existing launcher. Boot from normal application flash,
not ROM DFU. Preserve the user's corrected jumper orientation matching the product photos.
Do not flash this application at 0x08000000 or replace the launcher.

The original protected `main.c` initializes/adopts board state and invokes
`app_init()`, `board_background()`, `app_touch()` and `app_update()`. USB service
uses TinyUSB `tud_task()` and `tud_cdc_n_*` with physical CDC index **0 only**.
Logical channel numbers are protocol fields, not additional CDC indexes.

The calibrated touch path stays in the launcher SDK; it polls at about 5 ms,
suppresses repeated coordinates, and reports release edges. There is no new
touch SPI read or EXTI owner competing with the existing driver. The remote
canvas is **320x182**, translated to physical Y=30..211 so MENU, calibration,
and the framework footer remain available. Full-screen 320x240 drawing is
deliberately not supported by this launcher app.

`Display_Flush_Window()` writes real RGB565 pixels on the inherited T100 LCD
bus. Radio operations are explicit **MOCK loopback**, without configuring SPI
or transmitting RF. [Recovered radio wiring](docs/board-wiring.md) records the
earlier NRF scanner connections and unresolved CC1101 detection.

## ESP32 applications

`esp32/` is the small Arduino/PlatformIO transport bench. It sends a 16x16
display window each second, alternates the mock radio mode every three seconds,
sends an escaping/binary loopback test, listens for touch/radio/system events
in separate tasks, and prints link status every five seconds. UART `s` requests
status; uppercase `L` sends `SYS:LAUNCHER`.

`upstream/Bruce/` is the downloaded Bruce application with the USB HAT board
adaptation applied by `tools/integrate_bruce.py`. The selected target is
`usbhat-bruce`. The new **Others → USB HAT Bench** menu provides status,
CC1101/NRF24 mock loopback, and STM32 launcher return. It keeps Bruce's existing
menu/application framework, font and sprite rendering through a RAM-backed
LovyanGFX panel, and converts calibrated remote touch to Bruce input events.
Only changed 16x16 tiles are transferred; a shadow tile is committed after ACK.
Reconnect invalidates the shadow so all tiles refresh. About 233 KB of PSRAM
holds the framebuffer and shadow.

Existing Bruce RF tools still expect their native local drivers; this first
bench adaptation routes the RF/NRF menus to the USB bench menu and does **not**
claim the existing RF tools work over the new USB radio channel. Native
USB device/HID/MSC use conflicts with USB host and must stay disabled in this
target. SD file access on the STM32 is not exported by this protocol.

The default `HAT_VBUS_ENABLE_PIN=-1` does not invent a power-switch GPIO for a
generic board. Verify the ESP32 native connector supplies VBUS. Keep COM3
connected for console/programming and connect the **native** USB port to the
STM32 USB-Slave connector for the direct link. Neither USB initialization nor
a cable can create a missing host 5 V supply circuit.

## Wire protocol v1

Unescaped frame body (all multi-byte values big endian):

```
7E | version=01 channel type sequence:u16 length:u16 payload CRC16:u16 | 7E
```

Escape bytes 7E and 7D as `7D (byte XOR 20)`. CRC-16/CCITT-FALSE (poly 1021,
init FFFF, no reflection/xorout) covers version through payload. Payload limit
is 1024 bytes; partial-frame inactivity timeout is 500 ms. Delimiters recover
framing after malformed data; invalid lengths, dangling escapes and CRC errors
never invoke a hardware command.

| Channel | Type | Payload |
|---|---|---|
| 0 display | 1 window | Original `AA 01 x1:u16 y1:u16 x2:u16 y2:u16 bytes:u16 RGB565...`, inclusive bounds, pixels MSB first |
| 0 touch | 2 touch | `BB x:u16 y:u16 pressed:u8`; added flag reports release |
| 1 radio | 5 select | `AA 05 mode` (1 CC1101 mock, 2 NRF24 mock) |
| 1 radio | 6 TX | One complete binary payload; 1..61 CC mock or 1..32 NRF mock |
| 1 radio | 7 RX | Complete loopback payload, same sequence as TX |
| 2 system | 8 command | Exact `SYS:STATUS` or `SYS:LAUNCHER`, no newline |
| 2 system | 9 status | Bounded diagnostic text; includes MOCK and counters |
| 0/1/2 | 10 ACK | Original type, status (0 OK, 1 invalid, 2 unsupported, 3 busy) |
| 2 system | 11 ping | Empty; returns status |

System command parsing requires the whole exact framed payload. Arbitrary
radio bytes cannot trigger launcher reset. Reset waits a bounded 100 ms for
ACK progress, detaches/deinitializes USB, disables its IRQ, then uses the SDK
return routine which executes the software reset into the launcher.

Each channel has a bounded four-packet queue. Frame-level round robin prevents
display traffic from monopolizing transport; bytes from different frames never
interleave. The host permits one outstanding acknowledged request per channel,
validates sequence/type/status, times out without automatic replay, and resets
link state on disconnect. It does not promise hard real-time latency or lossless
operation under sustained overload; drops/timeouts are diagnostic failures.

## Build and validation

Dependencies are pinned in `dependencies.lock.json`; local public checkouts are
under `dependencies/`. `tools/prepare_dependencies.py` copies the selected
TinyUSB DCD/device sources and Espressif CDC host driver into buildable projects.
The installed launcher SDK supplies its own CMSIS/startup/linker and clock code;
HAL USB device middleware must not also own the OTG interrupt.

The source ZIP contains the complete STM32 project, the ESP32 bench/transport,
and the Bruce overlay/patch. Bruce's upstream base is pinned in
`upstream.lock.json`. For a fresh extraction, run `python tools/fetch_sources.py`
first. It fetches the pinned public repositories without resetting existing work.

```
python tools/prepare_dependencies.py
python tools/integrate_bruce.py
pio run -d esp32 -e usbhat-bench
pio run -d esp32 -e usbhat-display-check
pio run -d upstream/Bruce -e usbhat-bruce
```

Use the installed launcher `app_factory.py --root ./stm32-apps validate USBHAT`,
then `build USBHAT`, then `package USBHAT`. Any app source change needs a fresh
successful build receipt before packaging.

`tests/protocol_test.c` runs against the same C header used on both MCUs. It
checks the known CRC vector, every split boundary, bytewise/coalesced reads,
binary escaping, corruption and resynchronization, malformed and oversized
frames, timeouts including clock wrap, bounded FIFO ordering, and one million
fuzz bytes with parser guard words. Compile/run with a native C compiler.

`tools/pc_bench.py` is an optional pyserial smoke test with the STM32 attached
directly to a PC. It only opens a unique CAFE:4071 USBHAT device, requires a
status reply confirming MOCK radio, then checks fragmented display writes,
out-of-bounds rejection, and exact binary radio loopback. It does not issue
launcher return or flash commands. This PC test has not run on physical hardware.

The parser's native C tests passed, as did the STM32 launcher build/package,
the standalone ESP32 transport build, and the ESP32 framebuffer-driver build.
The modified Bruce target has also built successfully. See `dist/manifest.json`
for the exported images and checksums; hardware display qualification remains
open as described above.
ELF debug images are kept locally; the Git repository includes firmware binaries,
source and reproducible build inputs without the large ELF files or flash backups.

Build success is not physical USB or RF validation. The first device test is:
install/run STM32 app, start ESP32 bench, confirm CDC attach/status, touch events,
changing colored window, exact escaped-byte radio loopback, then launcher return.
Recalibration/resume, unplug/replug, sustained display updates and physical RF
remain separate bench checks. No hardware has been programmed by these build
and packaging operations.

## Tile diagnostics

ESP32 UART logs `TILE TX` and `TILE ACK` with sequence, inclusive x1/y1/x2/y2,
payload length, pixel-byte length, CRC and ACK status. The STM32 logs `STM TILE`
on its SDK UART and mirrors it through logical system channel 2, so its receipt,
full pixel-word count and ACK status are visible in the ESP32 console too.
For a 16x16 tile expect payload=524, pixels_bytes=512, words=256, ACK=0.
The last tile row is 16x6: payload=204, pixels_bytes=192, words=96.
Coordinates in the protocol are client coordinates; the LCD Y range is +30.
Per-tile UART tracing intentionally slows display updates during diagnosis.

`tests/window_test.c` checks all 240 tile positions and all 58,240 ordered pixel
words using the same LCD emitter compiled into the STM32 app. It verifies 2A/2B
register bytes, inclusive endpoints, the +30 Y offset, RGB565 ordering, clipped
bottom-row tiles, malformed lengths, zero dimensions and overflow rejection.
