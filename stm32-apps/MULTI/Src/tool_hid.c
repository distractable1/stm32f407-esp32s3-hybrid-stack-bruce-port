#include "tool_hid.h"
#include "board_support.h"
#include "stm32f4xx.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>
volatile uint32_t hid_irq_count;
extern volatile uint32_t hid_device_queries,hid_config_queries;
static bool initialized,guard,was_connected;
static unsigned phase,neutral;
static uint8_t mod,key,mouse_buttons;
static int mx,my,mw;
static bool mouse_pending;
static char text_buffer[256];static unsigned text_pos;
static const uint8_t ascii[128][2]={HID_ASCII_TO_KEYCODE};
void OTG_FS_IRQHandler(void){++hid_irq_count;tusb_int_handler(0,true);}
bool tool_hid_connected(void){return initialized&&tud_mounted()&&!tud_suspended();}
const char*tool_hid_status(void){static char s[64];if(tool_hid_connected())return "USB HID connected";snprintf(s,sizeof s,"USB %s I%lu D%lu C%lu",initialized?"wait":"FAIL",(unsigned long)hid_irq_count,(unsigned long)hid_device_queries,(unsigned long)hid_config_queries);return s;}
bool tool_hid_idle(void){return !phase&&!neutral&&!mouse_pending&&!text_buffer[text_pos];}
bool tool_hid_key(uint8_t m,uint8_t k){if(!tool_hid_connected()||!tool_hid_idle())return false;mod=m;key=k;phase=1;return true;}
bool tool_hid_character(uint8_t c){if(c>=128||!ascii[c][1])return false;return tool_hid_key(ascii[c][0]?2:0,ascii[c][1]);}
bool tool_hid_text(const char*s){if(!tool_hid_connected()||!tool_hid_idle()||strlen(s)>255)return false;for(unsigned i=0;s[i];++i)if((unsigned char)s[i]>=128||!ascii[(unsigned char)s[i]][1])return false;strcpy(text_buffer,s);text_pos=0;return true;}
bool tool_hid_mouse(int x,int y,uint8_t b,int w){if(!tool_hid_connected()||mouse_pending||tool_macro_busy()||x< -127||x>127||y< -127||y>127||w< -127||w>127||b>7)return false;mx=x;my=y;mw=w;mouse_buttons=b;mouse_pending=true;return true;}
void tool_hid_release(void){phase=0;text_buffer[0]=0;text_pos=0;mouse_pending=false;neutral=1;}
void tool_hid_task(uint32_t now){if(!initialized||guard)return;guard=true;tud_task_ext(0,false);bool connected=tool_hid_connected();if(!connected){if(was_connected)tool_macro_transport_lost();phase=0;neutral=0;mouse_pending=false;text_buffer[0]=0;text_pos=0;}was_connected=connected;
if(connected&&tud_hid_ready()){
 uint8_t keys[6]={0};
 if(neutral==1){if(tud_hid_keyboard_report(1,0,keys))neutral=2;}
 else if(neutral==2){if(tud_hid_mouse_report(2,0,0,0,0,0))neutral=0;}
 else if(phase==1){keys[0]=key;if(tud_hid_keyboard_report(1,mod,keys))phase=2;}
 else if(phase==2){if(tud_hid_keyboard_report(1,0,keys))phase=3;}
 else if(phase==3)phase=0;
 else if(mouse_pending){if(tud_hid_mouse_report(2,mouse_buttons,mx,my,mw,0))mouse_pending=false;}
 else if(text_buffer[text_pos]){uint8_t c=text_buffer[text_pos++];mod=ascii[c][0]?2:0;key=ascii[c][1];phase=1;}
}
if(connected){tool_macro_task(now);}
guard=false;}
bool tool_hid_init(void){if(initialized)return true;RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;GPIOA->MODER=(GPIOA->MODER&~((3u<<22)|(3u<<24)))|(2u<<22)|(2u<<24);GPIOA->AFR[1]=(GPIOA->AFR[1]&~((15u<<12)|(15u<<16)))|(10u<<12)|(10u<<16);GPIOA->OSPEEDR|=(3u<<22)|(3u<<24);RCC->AHB2ENR|=RCC_AHB2ENR_OTGFSEN;RCC->AHB2RSTR|=RCC_AHB2RSTR_OTGFSRST;__DSB();RCC->AHB2RSTR&=~RCC_AHB2RSTR_OTGFSRST;__DSB();*(volatile uint32_t*)0x50000804u|=2u;delay_ms(200);NVIC_SetPriority(OTG_FS_IRQn,1);tusb_rhport_init_t c={.role=TUSB_ROLE_DEVICE,.speed=TUSB_SPEED_FULL};initialized=tusb_init(0,&c);return initialized;}
void tool_hid_shutdown(void){tool_macro_cancel();tool_hid_release();uint32_t start=tusb_time_millis_api();while(initialized&&tool_hid_connected()&&(neutral||!tud_hid_ready())&&(uint32_t)(tusb_time_millis_api()-start)<100)tool_hid_task(tusb_time_millis_api());if(initialized){tud_disconnect();tusb_deinit(0);NVIC_DisableIRQ(OTG_FS_IRQn);}initialized=false;was_connected=false;phase=neutral=0;}
