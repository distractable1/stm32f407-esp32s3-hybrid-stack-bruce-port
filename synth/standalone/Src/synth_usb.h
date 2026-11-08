#ifndef SYNTH_USB_H
#define SYNTH_USB_H
#include <stdint.h>
#include <stdbool.h>
extern uint32_t SystemCoreClock;
extern bool synth_usb_initialized;
void synth_fx_adjust(unsigned index,int direction);
unsigned synth_fx_value(unsigned index);
extern volatile uint32_t synth_frames_in, synth_frames_out, synth_sample_count;
extern volatile bool synth_recording, synth_playing, synth_tone;
extern volatile unsigned synth_wave, synth_volume;
extern volatile bool synth_input_active, synth_output_active;
bool synth_clock_init(void);
void synth_usb_init(void);
void synth_poll(void);
void synth_audio_task(void);
void synth_note_on(unsigned note,unsigned velocity);
void synth_note_off(unsigned note);
void synth_record(void);
void synth_play(void);
#endif
