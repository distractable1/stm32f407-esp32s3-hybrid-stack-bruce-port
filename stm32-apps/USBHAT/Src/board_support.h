#ifndef BOARD_SUPPORT_H
#define BOARD_SUPPORT_H
#include <stdbool.h>
#include <stdint.h>
#define BLACK 0x0841
#define WHITE 0xffff
#define CYAN 0x07ff
#define GREEN 0x07e0
#define RED 0xf800
#define BOARD_HANDOFF_ADDRESS 0x2001ff00u
extern uint32_t SystemCoreClock;
uint32_t tusb_time_millis_api(void);
bool board_init(void);
bool board_was_inherited(void);
bool board_prepare_handoff(void);
void board_recalibrate(void);
bool board_touch(int *x,int *y);
unsigned physical_buttons(void);
void board_background(void);
void board_return_to_launcher(void) __attribute__((noreturn));
void delay_ms(unsigned ms);
void rectangle(int x,int y,int width,int height,unsigned color);
void text(int x,int y,const char *value,unsigned color);
void panel_button(int x,int y,int width,const char *label,unsigned active);
void board_status_line(int x,int y,const char *value,unsigned foreground,unsigned background);
void serial(const char *value);
void number(int value);
#endif
