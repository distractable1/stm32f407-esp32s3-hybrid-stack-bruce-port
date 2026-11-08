# STM32 T100 project backups

Captured 2026-11-08. Each project has its own branch. main remains unchanged.

| Branch | Status |
|---|---|
| [projects/launcher](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/launcher) | Recovered T100 launcher source, board SDK and original launcher/SD releases. Existing build artifacts are preserved; no new firmware build or board flash was performed. |
| [projects/launcher-sdk](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/launcher-sdk) | App factory, packaging tools, MCP plugin, template, docs and tests. Output and installed marketplace SDK core script hashes match. Machine-specific paths remain in original plugin config; adapt them on another PC. |
| [projects/synth](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/synth) | Standalone and launcher synth sources, Windows companion and original compiled artifacts. No new hardware validation. |
| [projects/touch](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/touch) | Launcher touch app source and original probe/calibration tools. These diagnostic raw images have different flash layouts; this backup does not authorize flashing them. |
| [projects/multi](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/multi) | Latest touchpad multi-tool source and SD APP. app.c matches the newest saved multi-tool-touchpad source archive. |
| [projects/hello](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/hello) | Current managed Hello Launcher example and its compiled SD APP. |
| [projects/nrf-workbench](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/nrf-workbench) | Historical standalone nRF workbench source archive and compiled BIN/ELF. Archive has only 19 entries and is not a complete standalone SDK; retain the launcher SDK/CMSIS sources too. |
| [projects/rfscope](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/rfscope) | Latest lab v0.2 source and exact compiled APP, SHA256 d15fae81b63fd178b0d5fbb750073554a8a390deed22cfc1c31dfdc5ca1f68ee. All source hashes match its successful ARM build receipt. Physical CC1101 tests and UI redesign remain open. Original PC RFSCOPE folder is unchanged. |
| [projects/usbhat](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/usbhat) | Existing USBHAT/Bruce repository snapshot including committed source and dist artifacts; no claim about unavailable PC-only edits. |
| [projects/bruce-esp32](https://github.com/distractable1/stm32f407-esp32s3-hybrid-stack-bruce-port/tree/projects/bruce-esp32) | Existing USBHAT/Bruce repository snapshot including committed source and dist artifacts; no claim about unavailable PC-only edits. |

Each added project includes BACKUP-MANIFEST.json. Existing release archives preserve the saved source packages. Build caches and downloaded dependency checkouts are excluded.
This inventory covers identified STM32/T100 work; unrelated PC projects were not added. Hardware test status is preserved rather than inferred from compilation.
