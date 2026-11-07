#pragma once
#include <Arduino.h>
#include "hat_protocol.h"

namespace HatLink {
struct Touch {uint16_t x,y;bool pressed;uint32_t at;};
bool begin();
bool connected();
uint32_t generation();
bool touch(Touch &value);
bool command(const char *value);
bool selectRadio(uint8_t mode);
bool radioTransmit(const uint8_t *p,size_t n);
bool window(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *rgb);
void status();
}
