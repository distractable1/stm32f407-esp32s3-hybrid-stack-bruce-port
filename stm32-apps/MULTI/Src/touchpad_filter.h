#ifndef TOUCHPAD_FILTER_H
#define TOUCHPAD_FILTER_H
#include <stdbool.h>
#include <stdint.h>
void pad_reset(void);
void pad_point(int x,int y,uint32_t now);
void pad_stop(void);
void pad_gain(int quarters);
int pad_gain_value(void);
void pad_smoothing(unsigned level);
unsigned pad_smoothing_value(void);
bool pad_poll(uint32_t now,bool connected,int*dx,int*dy);
void pad_commit(bool accepted);
#endif
