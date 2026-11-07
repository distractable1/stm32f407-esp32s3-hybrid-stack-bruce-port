#include "../shared/hat_window.h"
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct{unsigned index,pixels;hat_window window;const uint8_t *rgb;} bus;
static void capture(void *ctx,uint16_t value,bool data){
 bus *b=ctx;hat_window *v=&b->window;
 uint16_t expected[]={0x2a,v->x1>>8,v->x1&255,v->x2>>8,v->x2&255,
  0x2b,(v->y1+30)>>8,(v->y1+30)&255,(v->y2+30)>>8,(v->y2+30)&255,0x2c};
 if(b->index<11){CHECK(value==expected[b->index]);CHECK(data==(b->index!=0&&b->index!=5&&b->index!=10));}
 else{CHECK(data);CHECK(b->pixels<(unsigned)v->width*v->height);
  CHECK(value==hat_get16(b->rgb+2*b->pixels));b->pixels++;}
 b->index++;
}
int main(void){
 uint8_t payload[HAT_MAX_PAYLOAD];hat_window v;
 unsigned tile_count=0,total_pixels=0;
 for(unsigned y=0;y<182;y+=15)for(unsigned x=0;x<320;x+=32){
  unsigned h=y+15<=182?15:182-y;
  CHECK(hat_window_bounds(&v,x,y,32,h));
  payload[0]=0xaa;payload[1]=1;hat_put16(payload+2,x);hat_put16(payload+4,y);
  hat_put16(payload+6,x+31);hat_put16(payload+8,y+h-1);hat_put16(payload+10,64*h);
  for(unsigned i=0;i<32*h;i++)hat_put16(payload+12+i*2,(uint16_t)(i*257+x+y));
  hat_window decoded;CHECK(hat_window_decode(&decoded,payload,12+64*h));
  CHECK(decoded.width==32&&decoded.height==h&&decoded.pixel_bytes==64*h);
  bus b={.window=decoded,.rgb=payload+12};
  CHECK(hat_lcd_emit(&decoded,payload+12,capture,&b)==32*h);
  CHECK(b.pixels==32*h&&b.index==11+32*h);total_pixels+=b.pixels;tile_count++;
  CHECK(!hat_window_decode(&decoded,payload,11+64*h));
 }
 CHECK(tile_count==130&&total_pixels==320*182);
 CHECK(hat_window_bounds(&v,319,181,1,1));CHECK(v.x2==319&&v.y2==181&&v.pixel_bytes==2);
 CHECK(!hat_window_bounds(&v,319,181,2,1));CHECK(!hat_window_bounds(&v,0,0,0,16));
 CHECK(!hat_window_bounds(&v,0,0,65535,65535));CHECK(!hat_window_bounds(&v,0,0,320,182));
 payload[0]=0xaa;payload[1]=1;hat_put16(payload+2,16);hat_put16(payload+4,16);
 hat_put16(payload+6,15);hat_put16(payload+8,31);hat_put16(payload+10,512);
 CHECK(!hat_window_decode(&v,payload,524));
 puts("PASS: all 130 tile windows; 58,240 ordered pixel writes; inclusive bounds; Y+30; 32x15=480 words/960 bytes; bottom tiles=64 words/128 bytes; malformed/overflow rejection");
}
