#include "board_support.h"
#include <stdio.h>
static void draw(void){rectangle(0,0,320,240,BLACK);text(8,8,"TOUCH CANVAS",CYAN);panel_button(224,0,88,"MENU",0);text(8,208,"KEY1 clear | Hold WK_UP menu",WHITE);board_status_line(0,0,0,0,0);}
int main(void){if(!board_init())for(;;){}draw();int oldx=-1,oldy=-1;
for(;;){unsigned buttons=physical_buttons();if(buttons&1){board_recalibrate();draw();oldx=oldy=-1;}if(buttons&2){draw();oldx=oldy=-1;}if(buttons&8)board_return_to_launcher();int x,y;if(board_touch(&x,&y)){if(y<30&&x>=224)board_return_to_launcher();if(y>=30&&y<206){if(oldx<0){oldx=x;oldy=y;}int dx=x-oldx,dy=y-oldy;int steps=(dx<0?-dx:dx);if((dy<0?-dy:dy)>steps)steps=dy<0?-dy:dy;if(!steps)steps=1;for(int i=0;i<=steps;++i)rectangle(oldx+dx*i/steps,oldy+dy*i/steps,2,2,CYAN);oldx=x;oldy=y;}else oldx=oldy=-1;char line[39];snprintf(line,sizeof line,"X:%d Y:%d inherited:%s",x,y,board_was_inherited()?"yes":"no");board_status_line(8,226,line,WHITE,BLACK);}else oldx=oldy=-1;delay_ms(5);}}
