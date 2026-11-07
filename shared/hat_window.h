#ifndef HAT_WINDOW_H
#define HAT_WINDOW_H
#include "hat_protocol.h"
typedef struct {
 uint16_t x1,y1,x2,y2,width,height,pixel_bytes;
} hat_window;
static inline bool hat_window_bounds(hat_window *v,uint16_t x,uint16_t y,uint16_t w,uint16_t h){
 if(!v||!w||!h||x>=320||y>=182||w>320-x||h>182-y)return false;
 uint32_t bytes=(uint32_t)w*h*2;
 if(bytes>HAT_MAX_PAYLOAD-12)return false;
 v->x1=x;v->y1=y;v->x2=x+w-1;v->y2=y+h-1;
 v->width=w;v->height=h;v->pixel_bytes=(uint16_t)bytes;return true;
}
static inline bool hat_window_decode(hat_window *v,const uint8_t *p,uint16_t n){
 if(!p||n<12||p[0]!=0xaa||p[1]!=1)return false;
 uint16_t x=hat_get16(p+2),y=hat_get16(p+4),xe=hat_get16(p+6),ye=hat_get16(p+8);
 if(xe<x||ye<y||xe>=320||ye>=182)return false;
 return hat_window_bounds(v,x,y,xe-x+1,ye-y+1)&&
        hat_get16(p+10)==v->pixel_bytes&&n==12u+v->pixel_bytes;
}
/* Callback receives a 16-bit bus word; command parameters are single bytes.
 * CS must stay asserted across this entire call. The caller owns GPIO timing. */
typedef void (*hat_lcd_word_fn)(void *,uint16_t,bool);
static inline uint32_t hat_lcd_emit(const hat_window *v,const uint8_t *rgb,
                                   hat_lcd_word_fn write,void *ctx){
 if(!v||!rgb||!write)return 0;
 uint16_t y1=v->y1+30,y2=v->y2+30;
 write(ctx,0x2a,false);write(ctx,v->x1>>8,true);write(ctx,v->x1&255,true);
 write(ctx,v->x2>>8,true);write(ctx,v->x2&255,true);
 write(ctx,0x2b,false);write(ctx,y1>>8,true);write(ctx,y1&255,true);
 write(ctx,y2>>8,true);write(ctx,y2&255,true);write(ctx,0x2c,false);
 uint32_t count=(uint32_t)v->width*v->height;
 for(uint32_t i=0;i<count;i++)write(ctx,hat_get16(rgb+i*2),true);
 return count;
}
#endif
