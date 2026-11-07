# Source bases and notices

The modified Bruce firmware is based on BruceDevices/firmware at the revision
in `upstream.lock.json`. Its upstream AGPL-3.0-or-later license and third-party
attributions are preserved under `third_party/Bruce/`. The complete pinned
upstream source can be fetched with `tools/fetch_sources.py`; the local changes
are provided in `integrations/bruce/remote-display.patch` and the board overlay.

LovyanGFX's MIT/BSD notices are preserved under `third_party/LovyanGFX/`.
TinyUSB retains its notices in the STM32 project's `Licenses/` and
`Src/TinyUSB/LICENSE`. The Espressif CDC host driver's Apache-2.0 license is in
`esp32/lib/UsbCdcHost/LICENSE`. The inherited STM32 launcher framework retains
its original CMSIS, ST, TinyUSB and synth notices in the project's `Licenses/`.

Flash backups, local tool installations, account credentials and build caches
are not part of the published project.
