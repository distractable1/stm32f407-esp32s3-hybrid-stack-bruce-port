#include "app.h"
#include "board_support.h"
#include "hat_board.h"
#include "hat_protocol.h"
#include "hat_window.h"
#include "tusb.h"
#include <stdio.h>
const char app_display_name[]="USB Peripheral Bench";
#define LOG(s) serial("[USBHAT] " s "\r\n")
static bool started,servicing,reset_pending,tile_tracing;
static uint32_t reset_at,tx_dropped,touch_report_at;
static uint8_t radio_mode=1;
static uint16_t event_seq;
static hat_parser parser;
static hat_queue tx[3];
static uint8_t wire[HAT_WIRE_MAX];
static size_t wire_length,wire_offset;
static unsigned next_channel;
static bool touch_pressed,touch_dirty;
static uint16_t touch_x,touch_y;
static bool enqueue(const hat_packet *p){
 if(hat_push(&tx[p->channel],p))return true;
 tx_dropped++;return false;
}
static void ack(const hat_packet *p,uint8_t status){
 hat_packet a={.channel=p->channel,.type=HAT_ACK,.seq=p->seq,.length=2};
 a.payload[0]=p->type;a.payload[1]=status;enqueue(&a);
}
static void status(const hat_packet *p){
 hat_packet a={.channel=HAT_SYSTEM,.type=HAT_STATUS,.seq=p->seq};
 int n=snprintf((char *)a.payload,sizeof a.payload,
  "USBHAT v1; viewport=320x182; physical_y=30; radio=MOCK:%u; rx=%lu; crc=%lu; malformed=%lu; txdrop=%lu",
  radio_mode,(unsigned long)parser.good,(unsigned long)parser.bad_crc,
  (unsigned long)parser.malformed,(unsigned long)tx_dropped);
 a.length=(uint16_t)((n<0)?0:((unsigned)n<sizeof a.payload?(unsigned)n:sizeof a.payload-1));enqueue(&a);
}
static void tile_trace(const hat_packet *p,unsigned code,uint32_t words){
 if(!tile_tracing && code==HAT_OK)return;
 hat_packet trace={.channel=HAT_SYSTEM,.type=HAT_STATUS,.seq=p->seq};
 unsigned x=0,y=0,xe=0,ye=0,n=0;
 if(p->length>=12){x=hat_get16(p->payload+2);y=hat_get16(p->payload+4);
  xe=hat_get16(p->payload+6);ye=hat_get16(p->payload+8);n=hat_get16(p->payload+10);}
 int k=snprintf((char *)trace.payload,sizeof trace.payload,
  "STM TILE seq=%u x1=%u y1=%u x2=%u y2=%u payload=%u pixels_bytes=%u words=%lu physical_y=%u..%u ACK=%u crc=%04X",
  p->seq,x,y,xe,ye,p->length,n,(unsigned long)words,y+30,ye+30,code,
  p->length>=12?hat_crc(p->payload+12,p->length-12):0);
 trace.length=(uint16_t)(k>0?(unsigned)k:0);
 serial("[USBHAT] ");serial((const char *)trace.payload);serial("\r\n");
 enqueue(&trace);
}
static void receive(void *ctx,const hat_packet *p){
 (void)ctx;if(reset_pending)return;
 if(tx[p->channel].count==HAT_QUEUE_DEPTH){tx_dropped++;return;}
 if(p->channel==HAT_DISPLAY && p->type==HAT_WINDOW){
  hat_window v;
  if(!hat_window_decode(&v,p->payload,p->length)){
   ack(p,HAT_BAD_PACKET);tile_trace(p,HAT_BAD_PACKET,0);return;
  }
  unsigned code=Display_Flush_Window(v.x1,v.y1,v.width,v.height,p->payload+12)?HAT_OK:HAT_BAD_PACKET;
  ack(p,code);tile_trace(p,code,Display_Last_Pixel_Words());
 }else if(p->channel==HAT_RADIO && p->type==HAT_RADIO_SELECT){
  if(p->length!=3||p->payload[0]!=0xaa||p->payload[1]!=5||p->payload[2]<1||p->payload[2]>2){ack(p,HAT_BAD_PACKET);return;}
  radio_mode=p->payload[2];ack(p,HAT_OK);
 }else if(p->channel==HAT_RADIO && p->type==HAT_RADIO_TX){
  if(!p->length||!Radio_SPI_Transmit(radio_mode,p->payload,p->length)){ack(p,HAT_BAD_PACKET);return;}
  if(tx[HAT_RADIO].count>HAT_QUEUE_DEPTH-2){ack(p,HAT_BUSY);return;}
  hat_packet loop=*p;loop.type=HAT_RADIO_RX;enqueue(&loop);ack(p,HAT_OK);
 }else if(p->channel==HAT_SYSTEM && p->type==HAT_PING){status(p);}
 else if(p->channel==HAT_SYSTEM && p->type==HAT_COMMAND){
  if(p->length==12&&!memcmp(p->payload,"SYS:LAUNCHER",12)){
   ack(p,HAT_OK);reset_pending=true;reset_at=tusb_time_millis_api();LOG("launcher exit requested");
  }else if(p->length==10&&!memcmp(p->payload,"SYS:STATUS",10))status(p);
  else if(p->length==11&&!memcmp(p->payload,"SYS:TRACE=1",11)){tile_tracing=true;ack(p,HAT_OK);}
  else if(p->length==11&&!memcmp(p->payload,"SYS:TRACE=0",11)){tile_tracing=false;ack(p,HAT_OK);}
  else ack(p,HAT_UNSUPPORTED);
 }else ack(p,HAT_UNSUPPORTED);
}
static void pump_tx(void){
 if(!tud_cdc_n_connected(0))return;
 if(wire_offset==wire_length){
  hat_packet p;wire_length=wire_offset=0;
  for(unsigned i=0;i<3;i++){
   unsigned c=(next_channel+i)%3;
   if(hat_pop(&tx[c],&p)){wire_length=hat_encode(&p,wire,sizeof wire);next_channel=(c+1)%3;break;}
  }
 }
 if(wire_offset<wire_length){
  uint32_t avail=tud_cdc_n_write_available(0);
  size_t n=wire_length-wire_offset;if(n>avail)n=avail;
  if(n)wire_offset+=tud_cdc_n_write(0,wire+wire_offset,n);
  tud_cdc_n_write_flush(0);
 }
}
void board_background(void){
 if(started&&!servicing){servicing=true;tud_task();servicing=false;}
}
void tud_umount_cb(void){
 memset(&parser,0,sizeof parser);memset(tx,0,sizeof tx);
 wire_length=wire_offset=0;touch_pressed=false;touch_dirty=true;
}
/* VBUS sensing is intentionally off because PA9 belongs to the SDK UART.
 * A new USB configuration must still discard any old partial frame/queue. */
void tud_mount_cb(void){tud_umount_cb();}
void app_init(void){
 memset(&parser,0,sizeof parser);memset(tx,0,sizeof tx);wire_length=wire_offset=0;
 reset_pending=false;touch_pressed=false;touch_dirty=true;
 rectangle(0,30,320,182,BLACK);text(8,48,"USB HAT: CDC / 3 logical channels",CYAN);
 text(8,70,"Radio: MOCK loopback; no RF TX",WHITE);
 started=hat_usb_start();LOG("starting CDC bench (mock radio)");
 if(!started)text(8,96,"USB initialization FAILED",RED);
}
void app_update(uint32_t now){
 if(!started)return;
 board_background();uint8_t buf[128];unsigned budget=2048;
 while(budget&&tud_cdc_n_available(0)){
  uint32_t n=tud_cdc_n_read(0,buf,budget<sizeof buf?budget:sizeof buf);if(!n)break;
  hat_feed(&parser,buf,n,now,receive,NULL);budget-=n;
 }
 hat_parser_tick(&parser,now);
 if(touch_dirty || (uint32_t)(now-touch_report_at)>=100){
  hat_packet p={.channel=HAT_DISPLAY,.type=HAT_TOUCH,.seq=event_seq++,.length=6};
  p.payload[0]=0xbb;hat_put16(p.payload+1,touch_x);hat_put16(p.payload+3,touch_y);p.payload[5]=touch_pressed;
  if(enqueue(&p)){touch_dirty=false;touch_report_at=now;}
 }
 pump_tx();
 if(reset_pending && (uint32_t)(now-reset_at)>=100){app_shutdown();board_return_to_launcher();}
}
void app_touch(int x,int y,bool pressed){
 touch_x=(uint16_t)x;touch_y=(uint16_t)(y-30);touch_pressed=pressed;touch_dirty=true;
}
void app_button(unsigned buttons){(void)buttons;}
void app_shutdown(void){if(started){started=false;hat_usb_stop();LOG("USB stopped");}}
