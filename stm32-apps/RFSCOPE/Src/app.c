#include "app.h"
#include "board_support.h"
#include "cc1101.h"
#include <stdio.h>
const char app_display_name[]="RF Scope D03";
static bool ready,enabled,busy,contact,ook;
static unsigned power,interval=2,trace_x;
static uint32_t khz=433920,sequence,rx_count,last_tx,tx_start,last_sample,last_draw;
static int rssi=-120;
static unsigned touch_count,button_count,retry_count;
static int touch_x=-1,touch_y=-1;
static uint32_t feedback_until;
static const char *last_input="NONE";
static const unsigned periods[]={100,250,500,1000};
static const int powers[]={-30,-20,-15,-10};
static const char *message="RX ready";
static void controls(void){
 char b[30];
 panel_button(4,136,100,enabled?"STOP TX":"TX OFF",enabled);
 snprintf(b,sizeof b,"PWR %d",powers[power]);panel_button(110,136,96,b,0);
 snprintf(b,sizeof b,"%ums",periods[interval]);panel_button(212,136,104,b,0);
 panel_button(4,173,72,"FREQ -",0);panel_button(82,173,72,"FREQ +",0);
 panel_button(160,173,74,ook?"OOK":"2-FSK",ook);panel_button(240,173,76,"RETRY",0);
}
static void diagnostics(void){
 char b[64];rectangle(0,74,320,18,BLACK);
 snprintf(b,sizeof b,"IN:%s T:%u B:%u R:%u",last_input,touch_count,button_count,retry_count);
 text(4,76,b,CYAN);
 rectangle(0,92,320,16,BLACK);
 snprintf(b,sizeof b,"%s SO:%u/%u ST:%02X P14:SET",cc_stage,cc_so_idle,cc_so_selected,cc_spi_status);
 text(4,93,b,WHITE);
}
static void summary(void){
 char b[60];rectangle(0,30,320,42,BLACK);
 snprintf(b,sizeof b,"%lu.%03lu MHz  RSSI %d dBm",(unsigned long)(khz/1000),(unsigned long)(khz%1000),rssi);text(4,32,b,WHITE);
 snprintf(b,sizeof b,"TX:%lu RX:%lu %s",(unsigned long)sequence,(unsigned long)rx_count,busy?"SENDING":message);text(4,47,b,ready?GREEN:RED);
 if(cc_ids_read)snprintf(b,sizeof b,"CC part:%02X ver:%02X %s",cc_part,cc_version,ready?"SPI OK":"FAIL");
 else snprintf(b,sizeof b,"CC part:-- ver:-- NO ID READ");
 text(4,62,b,ready?CYAN:RED);
}
static void fail(void){enabled=busy=false;ready=false;message="RADIO ERROR";cc_close();controls();summary();}
static void start_radio(void){
 enabled=busy=false;ready=cc_init(khz,power,ook);message=ready?"RX ready":(cc_error?cc_error:"INIT FAILED");
 if(!ready)cc_close();
 controls();summary();diagnostics();
}
void app_init(void){
 contact=false;rectangle(0,30,320,182,BLACK);trace_x=0;
 text(4,93,"RSSI",WHITE);
 rectangle(4,109,312,1,WHITE);rectangle(4,129,312,1,WHITE);
 start_radio();last_tx=last_sample=tusb_time_millis_api();
}
void app_update(uint32_t now){
 if(feedback_until && (int32_t)(now-feedback_until)>=0){feedback_until=0;controls();diagnostics();}
 if(!ready)return;
 if(busy){
  bool done=false;
  if(!cc_poll(&done)||now-tx_start>100){fail();return;}
  if(done){busy=false;message="RX ready";}
 }
 if(!busy){
  if(!cc_receive(&rx_count)){fail();return;}
  if(now-last_sample>=25){
   last_sample=now;int sample;
   if(cc_rssi(&sample)){
    rssi=sample;int level=sample;if(level<-120)level=-120;if(level>-20)level=-20;
    int x=4+(trace_x++%312);rectangle(x,110,1,19,BLACK);rectangle(x,128-(level+120)*18/100,1,2,CYAN);
   }
  }
  if(enabled&&now-last_tx>=periods[interval]){
   last_tx=tx_start=now;
   if(!cc_send(sequence)){fail();return;}++sequence;busy=true;
  }
 }
 if(now-last_draw>=200){last_draw=now;summary();diagnostics();}
}
void app_touch(int x,int y,bool pressed){
 bool edge=pressed&&!contact;contact=pressed;if(!edge)return;
 touch_count++;touch_x=x;touch_y=y;last_input="TOUCH";
 feedback_until=tusb_time_millis_api()+170;
 if(y>=136&&y<164){
  if(x<106){if(ready){enabled=!enabled;if(!enabled){cc_stop();busy=false;message="TX stopped";}last_tx=tusb_time_millis_api();}}
  else if(x<210){power=(power+1)%4;start_radio();}
  else interval=(interval+1)%4;
 }
 if(y>=173&&y<201){
  if(x<78){if(khz>433050)khz-=10;start_radio();}
  else if(x<156){if(khz<434790)khz+=10;start_radio();}
  else if(x<236){ook=!ook;start_radio();}
  else {retry_count++;start_radio();}
 }
 controls();
 if(y>=136&&y<164){if(x<106)panel_button(4,136,100,"TX",1);else if(x<210)panel_button(110,136,96,"PWR",1);else panel_button(212,136,104,"INTERVAL",1);}
 if(y>=173&&y<201){if(x<78)panel_button(4,173,72,"FREQ -",1);else if(x<156)panel_button(82,173,72,"FREQ +",1);else if(x<236)panel_button(160,173,74,"MODE",1);else panel_button(240,173,76,"RETRY",1);}
 summary();diagnostics();
}
void app_button(unsigned buttons){
 button_count++;last_input=(buttons&APP_BUTTON_KEY1)?"KEY1":"WK_UP";
 if(buttons&APP_BUTTON_KEY1){enabled=busy=false;cc_stop();if(ready)message="TX stopped";}
 controls();summary();diagnostics();
}
void app_shutdown(void){enabled=busy=ready=false;contact=false;cc_close();}
