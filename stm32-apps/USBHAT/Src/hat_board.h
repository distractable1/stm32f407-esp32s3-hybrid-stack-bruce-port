#pragma once
#include <stdbool.h>
#include <stdint.h>
bool hat_usb_start(void);
void hat_usb_stop(void);
bool Display_Flush_Window(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *rgb565);
uint32_t Display_Last_Pixel_Words(void);
bool Radio_SPI_Transmit(uint8_t mode,const uint8_t *data,uint16_t length);
