# Optional future SPI transport

Keep USB as the current default. A dedicated short SPI link with queued DMA,
length/CRC/sequence framing and a READY/IRQ handshake may reduce latency and
increase throughput. These are engineering expectations, not measurements on
this hardware. Single-ended jumper wiring is not inherently more robust than
USB's differential physical link. Use 3.3V signals and a shared ground.

MicroSD sniffers can expose socket contacts, but are not protocol converters.
This launcher SDK bit-bangs its SD interface: PC12 clock, PD2 output, PC8 input,
PC11 chip select (see protected Src/sd_diskio.c). Do not assume that is a
hardware-SPI-slave pin mapping. Repurposing the socket also conflicts with
normal SD card ownership and launcher/file access. An interposer does not
supply arbitration or guarantee which auxiliary contacts are routed.

Preferred exploration: verify a dedicated unused hardware SPI pin group on the
STM32 and free ESP32 pins; ESP32 master, STM32 slave with DMA, separate READY
(or master polling), explicit timeouts and bounded queues. Retain the current
logical channels and framing above a transport abstraction. Do not modify the
protected launcher SD implementation to retrofit this during radio bring-up.

Espressif reference (handshake and DMA considerations):
https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/peripherals/spi_slave.html
