#ifndef ENGINE_USB_H
#define ENGINE_USB_H
#include <stdint.h>
void engine_init(void);
void engine_note_on(unsigned note,unsigned velocity);
void engine_note_off(unsigned note);
void engine_render(int16_t *output);
void engine_wave(unsigned wave);
void engine_parameter(unsigned parameter,unsigned value);
unsigned engine_block_size(void);
#endif
