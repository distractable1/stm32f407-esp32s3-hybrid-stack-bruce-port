# T100 USB jumper setting confirmed on this board

Leave the jumper connecting PA9 to PA10 OPEN for this setup. The user identified
that bridge and confirmed that removing it restored the USB connection.

This is a confirmed board configuration observation. The exact electrical
mechanism has not been established. Do not attribute the fix to a USB firmware
change or require more USB startup changes without a new failure.

Normal application boot remains BT0 to GND with BT1 removed. Use USB-Slave for
native HID/audio. These boot selectors are separate from the PA9–PA10 bridge.
