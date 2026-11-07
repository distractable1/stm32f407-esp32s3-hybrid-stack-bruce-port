#pragma once
#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_FrameBufferBase.hpp>
#include <HatLink.h>
#include "esp_heap_caps.h"

inline SemaphoreHandle_t HatGraphicsMutex(){
 static SemaphoreHandle_t value=xSemaphoreCreateRecursiveMutex();return value;
}
struct HatGraphicsGuard {
 SemaphoreHandle_t value;
 HatGraphicsGuard():value(HatGraphicsMutex()){if(value)xSemaphoreTakeRecursive(value,portMAX_DELAY);}
 ~HatGraphicsGuard(){if(value)xSemaphoreGiveRecursive(value);}
};

/* RAM-backed Lovyan panel: all Bruce drawing and sprites use its usual API.
 * The worker sends only changed 32x15 RGB565 tiles and commits its shadow only
 * after a device ACK. Snapshot/ACK/shadow are independent of ongoing drawing. */
class HatPanel : public lgfx::Panel_FrameBufferBase {
public:
 HatPanel(){
  _cfg.panel_width=_cfg.memory_width=320;
  _cfg.panel_height=_cfg.memory_height=182;
  _cfg.pin_cs=_cfg.pin_rst=_cfg.pin_busy=-1;
  _cfg.readable=true;_auto_display=false;
 }
 bool init(bool reset) override {
  if(!mutex)mutex=HatGraphicsMutex();
  if(!mutex)return false;
  if(!pixels){
   pixels=(uint8_t *)heap_caps_calloc(320*182,2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
   frame=(uint8_t *)heap_caps_malloc(320*182*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
   shadow=(uint8_t *)heap_caps_malloc(320*182*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
   if(!pixels||!shadow||!frame)return false;
   for(unsigned y=0;y<182;y++)lines[y]=pixels+y*640;
   _lines_buffer=lines;
  }
  if(!lgfx::Panel_FrameBufferBase::init(reset))return false;
  if(!worker && xTaskCreate(task,"hat-display",6144,this,2,&worker)!=pdPASS)return false;
  return true;
 }
 void beginTransaction() override {if(mutex)xSemaphoreTakeRecursive(mutex,portMAX_DELAY);}
 void endTransaction() override {if(mutex)xSemaphoreGiveRecursive(mutex);}
 lgfx::color_depth_t setColorDepth(lgfx::color_depth_t) override {
  _write_depth=_read_depth=lgfx::color_depth_t::rgb565_2Byte;return _write_depth;
 }
 /* This bench board keeps a fixed landscape viewport and visible launcher UI. */
 void setRotation(uint_fast8_t) override {lgfx::Panel_FrameBufferBase::setRotation(0);}
private:
 SemaphoreHandle_t mutex=nullptr;TaskHandle_t worker=nullptr;
 uint8_t *pixels=nullptr,*shadow=nullptr,*frame=nullptr,*lines[182];
 static void task(void *arg){static_cast<HatPanel *>(arg)->run();}
 void run(){
  uint32_t previous_epoch=UINT32_MAX;bool valid[130]={};
  uint8_t snapshot[960];
  for(;;){
   if(previous_epoch!=HatLink::generation()){
    previous_epoch=HatLink::generation();memset(valid,0,sizeof valid);
   }
   if(!HatLink::connected()){vTaskDelay(pdMS_TO_TICKS(50));continue;}
   /* One consistent generation per sweep: never combine tiles from different
    * application frames while the USB worker waits for ACKs. */
   beginTransaction();memcpy(frame,pixels,320*182*2);endTransaction();
   for(unsigned ty=0;ty<13 && HatLink::connected();ty++)for(unsigned tx=0;tx<10 && HatLink::connected();tx++){
    unsigned x=tx*32,y=ty*15,h=(y+15<=182)?15:182-y,id=ty*10+tx;
    bool changed=!valid[id];
    for(unsigned row=0;row<h;row++){
     const uint8_t *src=frame+(y+row)*640+x*2;
     memcpy(snapshot+row*64,src,64);
     if(valid[id] && memcmp(src,shadow+(y+row)*640+x*2,64))changed=true;
    }
    if(!changed)continue;
    /* Lovyan framebuffer rgb565_2Byte uses swapped bytes for panel writes:
     * the in-memory byte stream is already MSB first. */
    if(HatLink::window(x,y,32,h,snapshot)){
     for(unsigned row=0;row<h;row++)memcpy(shadow+(y+row)*640+x*2,snapshot+row*64,64);
     valid[id]=true;
    }else{valid[id]=false;vTaskDelay(pdMS_TO_TICKS(50));}
   }
   vTaskDelay(pdMS_TO_TICKS(10));
  }
 }
};
