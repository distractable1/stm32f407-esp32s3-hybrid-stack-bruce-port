#include "HatLink.h"
#include "hat_window.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"
#include <atomic>
#include "freertos/semphr.h"
#include "freertos/queue.h"
#ifndef HAT_VBUS_ENABLE_PIN
#define HAT_VBUS_ENABLE_PIN -1
#endif
#ifndef HAT_TILE_TRACE
#define HAT_TILE_TRACE 0
#endif
#define LOG(fmt,...) Serial.printf("[HAT %lu] " fmt "\n",(unsigned long)millis(),##__VA_ARGS__)
namespace {
SemaphoreHandle_t lock,requests[3];
QueueHandle_t inbound[3],acks[3];
hat_queue outbound[3];hat_parser parser;
std::atomic<bool> online{false},detached{false};
std::atomic<uint32_t> epoch{0};
uint16_t sequence[3];
HatLink::Touch lastTouch{};
uint8_t scan_bins[126]={};uint32_t scan_revision=0;
char radio_info[240]="No STM32 radio diagnostic received";
std::atomic<int> radio_result{-1};
portMUX_TYPE touchLock=portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> drops{0};
bool initialization_attempted=false,initialization_ok=false;
void decoded(void *,const hat_packet *p){
 QueueHandle_t q=(p->type==HAT_ACK)?acks[p->channel]:inbound[p->channel];
 if(xQueueSend(q,p,0)!=pdTRUE)drops++;
}
bool data_cb(const uint8_t *data,size_t n,void *){
 xSemaphoreTake(lock,portMAX_DELAY);
 hat_feed(&parser,data,n,millis(),decoded,nullptr);
 xSemaphoreGive(lock);return true;
}
void event_cb(const cdc_acm_host_dev_event_data_t *event,void *){
 if(event->type==CDC_ACM_HOST_DEVICE_DISCONNECTED){online=false;detached=true;}
 else if(event->type==CDC_ACM_HOST_ERROR)LOG("USB driver error=%d",event->data.error);
}
void daemon(void *){
 for(;;){uint32_t flags=0;esp_err_t e=usb_host_lib_handle_events(pdMS_TO_TICKS(100),&flags);
  if(e!=ESP_OK&&e!=ESP_ERR_TIMEOUT)LOG("host events: %s",esp_err_to_name(e));
  if(flags&USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)usb_host_device_free_all();
 }
}
void reset_session(){
 xSemaphoreTake(lock,portMAX_DELAY);
 memset(&parser,0,sizeof parser);memset(outbound,0,sizeof outbound);
 for(unsigned c=0;c<3;c++){xQueueReset(inbound[c]);xQueueReset(acks[c]);}
 xSemaphoreGive(lock);
 portENTER_CRITICAL(&touchLock);lastTouch.pressed=false;lastTouch.at=millis();portEXIT_CRITICAL(&touchLock);
}
void usb_worker(void *){
 cdc_acm_dev_hdl_t dev=nullptr;unsigned next=0;
 uint8_t wire[HAT_WIRE_MAX];hat_packet p;
 for(;;){
  if(detached.exchange(false)){
   online=false;if(dev){cdc_acm_host_close(dev);dev=nullptr;}
   epoch++;reset_session();LOG("disconnected");
  }
  if(!dev){
   cdc_acm_host_device_config_t cfg={};cfg.connection_timeout_ms=500;
   cfg.out_buffer_size=HAT_WIRE_MAX;cfg.in_buffer_size=512;
   cfg.event_cb=event_cb;cfg.data_cb=data_cb;
   esp_err_t e=cdc_acm_host_open(HAT_VID,HAT_PID,0,&cfg,&dev);
   if(e!=ESP_OK){vTaskDelay(pdMS_TO_TICKS(100));continue;}
   e=cdc_acm_host_set_control_line_state(dev,true,false);
   if(e!=ESP_OK){cdc_acm_host_close(dev);dev=nullptr;continue;}
   epoch++;online=true;LOG("CDC connected, logical channels 0/1/2 (epoch %lu)",(unsigned long)epoch.load());
  }
  bool have=false;
  xSemaphoreTake(lock,portMAX_DELAY);
  hat_parser_tick(&parser,millis());
  for(unsigned i=0;i<3;i++){
   unsigned c=(next+i)%3;
   if(hat_pop(&outbound[c],&p)){next=(c+1)%3;have=true;break;}
  }
  xSemaphoreGive(lock);
  if(have){
   size_t n=hat_encode(&p,wire,sizeof wire);
   esp_err_t e=cdc_acm_host_data_tx_blocking(dev,wire,n,250);
   if(e!=ESP_OK){LOG("TX failed: %s; resetting session",esp_err_to_name(e));online=false;detached=true;}
  }else vTaskDelay(pdMS_TO_TICKS(1));
 }
}
void touch_worker(void *){
 hat_packet p;
 for(;;)if(xQueueReceive(inbound[0],&p,pdMS_TO_TICKS(100))==pdTRUE){
  if(p.type==HAT_TOUCH&&p.length==6&&p.payload[0]==0xbb){
   HatLink::Touch t{hat_get16(p.payload+1),hat_get16(p.payload+3),p.payload[5]!=0,millis()};
   if(!t.pressed || (t.x<320&&t.y<182)){portENTER_CRITICAL(&touchLock);lastTouch=t;portEXIT_CRITICAL(&touchLock);}
  }
 }
}
void radio_worker(void *){
 hat_packet p;
 for(;;)if(xQueueReceive(inbound[1],&p,portMAX_DELAY)==pdTRUE){
  if(p.type==HAT_RADIO_RX){
   LOG("RADIO RX seq=%u len=%u (real NRF24)",p.seq,p.length);
   for(unsigned i=0;i<p.length;i++)Serial.printf("%02X%s",p.payload[i],i+1==p.length?"\n":" ");
  }
 }
}
void system_worker(void *){
 hat_packet p;
 for(;;)if(xQueueReceive(inbound[2],&p,portMAX_DELAY)==pdTRUE){
  if(p.type==HAT_STATUS&&p.length==134&&!memcmp(p.payload,"NRFSCAN ",8)){
   bool valid=true;for(unsigned i=0;i<126;i++)if(p.payload[8+i]!='0'&&p.payload[8+i]!='1')valid=false;
   if(valid){portENTER_CRITICAL(&touchLock);for(unsigned i=0;i<126;i++)scan_bins[i]=p.payload[8+i]-'0';scan_revision++;portEXIT_CRITICAL(&touchLock);}
  }
  if(p.type==HAT_STATUS&&p.length>=6&&!memcmp(p.payload,"NRF24 ",6)){
   portENTER_CRITICAL(&touchLock);size_t n=p.length<sizeof radio_info-1?p.length:sizeof radio_info-1;
   memcpy(radio_info,p.payload,n);radio_info[n]=0;portEXIT_CRITICAL(&touchLock);
  }
  LOG("SYSTEM seq=%u: %.*s",p.seq,p.length,(const char *)p.payload);
 }
}
bool send(hat_packet &p,bool wait_ack){
 const bool select=p.channel==HAT_RADIO&&p.type==HAT_RADIO_SELECT;
 if(select)radio_result=-2;
 if(!online){if(select)radio_result=-1;return false;}
 if(xSemaphoreTake(requests[p.channel],pdMS_TO_TICKS(600))!=pdTRUE){if(select)radio_result=-3;return false;}
 const uint32_t start_epoch=epoch.load();
 p.seq=++sequence[p.channel];
 const bool tile=p.channel==HAT_DISPLAY&&p.type==HAT_WINDOW;
 if(tile&&HAT_TILE_TRACE)LOG("TILE TX seq=%u x1=%u y1=%u x2=%u y2=%u payload=%u pixels_bytes=%u crc=%04X",
  p.seq,hat_get16(p.payload+2),hat_get16(p.payload+4),hat_get16(p.payload+6),
  hat_get16(p.payload+8),p.length,hat_get16(p.payload+10),hat_crc(p.payload+12,p.length-12));
 xSemaphoreTake(lock,portMAX_DELAY);
 bool queued=online&&hat_push(&outbound[p.channel],&p);
 xSemaphoreGive(lock);
 if(select&&!queued)radio_result=-4;
 bool ok=queued&&!wait_ack;
 if(queued&&wait_ack){
  uint32_t start=millis();hat_packet a;
  while(online&&epoch.load()==start_epoch&&(uint32_t)(millis()-start)<600){
   if(xQueueReceive(acks[p.channel],&a,pdMS_TO_TICKS(20))==pdTRUE && a.seq==p.seq &&
      a.length==2&&a.payload[0]==p.type){ok=a.payload[1]==HAT_OK;
      if(select){radio_result=a.payload[1];LOG("REMOTE RADIO ACK seq=%u status=%u",p.seq,a.payload[1]);}
      if(tile&&HAT_TILE_TRACE)LOG("TILE ACK seq=%u x1=%u y1=%u x2=%u y2=%u payload=%u status=%u",
       p.seq,hat_get16(p.payload+2),hat_get16(p.payload+4),hat_get16(p.payload+6),
       hat_get16(p.payload+8),p.length,a.payload[1]);break;}
  }
  if(!ok)LOG("request failed/timeout ch=%u type=%u seq=%u",p.channel,p.type,p.seq);
  if(tile&&!ok)LOG("TILE FAIL seq=%u payload=%u ACK=missing-or-rejected",p.seq,p.length);
 }
 xSemaphoreGive(requests[p.channel]);return ok;
}
}
namespace HatLink {
void radioDiagnostic(char *out,size_t n){
 const int result=radio_result.load();
 const char *reason=result==-1?"USB bridge disconnected":result==-2?"USB radio ACK missing":
 result==-3?"Radio request busy":result==-4?"USB radio queue full":
 result==HAT_UNSUPPORTED?"STM32 radio unsupported":result==HAT_BUSY?"STM32 radio busy":
 result==HAT_OK?"STM32 radio ready":"STM32 probe rejected";
 snprintf(out,n,"%s",reason);
 char detail[240];portENTER_CRITICAL(&touchLock);memcpy(detail,radio_info,sizeof detail);portEXIT_CRITICAL(&touchLock);
 LOG("REMOTE RADIO diagnostic: %s",detail);
}
uint32_t scanSnapshot(uint8_t *bins){portENTER_CRITICAL(&touchLock);memcpy(bins,scan_bins,126);uint32_t rev=scan_revision;portEXIT_CRITICAL(&touchLock);return rev;}
bool begin(){
 if(initialization_attempted)return initialization_ok;
 initialization_attempted=true;
 lock=xSemaphoreCreateMutex();if(!lock)return false;
 for(unsigned c=0;c<3;c++){
  inbound[c]=xQueueCreate(4,sizeof(hat_packet));acks[c]=xQueueCreate(4,sizeof(hat_packet));
  requests[c]=xSemaphoreCreateMutex();
  if(!inbound[c]||!acks[c]||!requests[c])return false;
 }
 if(HAT_VBUS_ENABLE_PIN>=0){pinMode(HAT_VBUS_ENABLE_PIN,OUTPUT);digitalWrite(HAT_VBUS_ENABLE_PIN,HIGH);}
 usb_host_config_t cfg={};cfg.skip_phy_setup=false;cfg.intr_flags=ESP_INTR_FLAG_LEVEL1;
 esp_err_t e=usb_host_install(&cfg);
 if(e!=ESP_OK){LOG("usb_host_install: %s",esp_err_to_name(e));return false;}
 if(xTaskCreate(daemon,"usb-events",4096,nullptr,5,nullptr)!=pdPASS)return false;
 cdc_acm_host_driver_config_t driver={};driver.driver_task_stack_size=8192;
 driver.driver_task_priority=5;driver.xCoreID=0;
 e=cdc_acm_host_install(&driver);if(e!=ESP_OK){LOG("CDC install: %s",esp_err_to_name(e));return false;}
 initialization_ok=xTaskCreate(usb_worker,"hat-usb",8192,nullptr,4,nullptr)==pdPASS &&
  xTaskCreate(touch_worker,"hat-touch",4096,nullptr,3,nullptr)==pdPASS &&
  xTaskCreate(radio_worker,"hat-radio",4096,nullptr,2,nullptr)==pdPASS &&
  xTaskCreate(system_worker,"hat-system",4096,nullptr,2,nullptr)==pdPASS;
 return initialization_ok;
}
bool connected(){return online;}
uint32_t generation(){return epoch;}
bool touch(Touch &value){
 portENTER_CRITICAL(&touchLock);value=lastTouch;portEXIT_CRITICAL(&touchLock);
 if(!online || (uint32_t)(millis()-value.at)>350)value.pressed=false;
 return value.pressed;
}
bool command(const char *s){
 hat_packet p={};p.channel=HAT_SYSTEM;p.type=HAT_COMMAND;
 size_t n=strlen(s);if(n>HAT_MAX_PAYLOAD)return false;
 p.length=n;memcpy(p.payload,s,n);return send(p,true);
}
bool selectRadio(uint8_t mode){
 if(mode<1||mode>2)return false;
 hat_packet p={};p.channel=HAT_RADIO;p.type=HAT_RADIO_SELECT;p.length=3;
 p.payload[0]=0xaa;p.payload[1]=5;p.payload[2]=mode;return send(p,true);
}
bool radioTransmit(const uint8_t *data,size_t n){
 if(!n||n>61)return false;
 hat_packet p={};p.channel=HAT_RADIO;p.type=HAT_RADIO_TX;p.length=n;
 memcpy(p.payload,data,n);return send(p,true);
}
bool window(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *rgb){
 hat_window v;
 if(!rgb||!hat_window_bounds(&v,x,y,w,h))return false;
 uint32_t n=v.pixel_bytes;
 hat_packet p={};p.channel=HAT_DISPLAY;p.type=HAT_WINDOW;p.length=n+12;
 p.payload[0]=0xaa;p.payload[1]=1;hat_put16(p.payload+2,x);hat_put16(p.payload+4,y);
 hat_put16(p.payload+6,x+w-1);hat_put16(p.payload+8,y+h-1);hat_put16(p.payload+10,n);
 memcpy(p.payload+12,rgb,n);return send(p,true);
}
void status(){
 LOG("HOST RX queue drops=%lu",(unsigned long)drops.load());
 hat_packet p={};p.channel=HAT_SYSTEM;p.type=HAT_PING;send(p,false);
}
}
