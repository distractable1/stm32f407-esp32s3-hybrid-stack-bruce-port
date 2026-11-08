#include "tusb.h"
volatile uint32_t hid_device_queries,hid_config_queries;
static const uint8_t report[]={TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(1)),TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(2))};
static const tusb_desc_device_t device={.bLength=18,.bDescriptorType=1,.bcdUSB=0x200,.bMaxPacketSize0=64,.idVendor=0xcafe,.idProduct=0x4079,.bcdDevice=0x101,.iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1};
const uint8_t*tud_descriptor_device_cb(void){++hid_device_queries;return(const uint8_t*)&device;}
static const uint8_t config[]={TUD_CONFIG_DESCRIPTOR(1,1,0,9+TUD_HID_DESC_LEN,0,250),TUD_HID_DESCRIPTOR(0,0,HID_ITF_PROTOCOL_NONE,sizeof(report),0x81,16,5)};
const uint8_t*tud_descriptor_configuration_cb(uint8_t i){(void)i;++hid_config_queries;return config;}
const uint8_t*tud_hid_descriptor_report_cb(uint8_t i){(void)i;return report;}
uint16_t tud_hid_get_report_cb(uint8_t i,uint8_t id,hid_report_type_t type,uint8_t*b,uint16_t n){(void)i;unsigned size=type==HID_REPORT_TYPE_INPUT?(id==1?8:id==2?5:0):type==HID_REPORT_TYPE_OUTPUT&&id==1?1:0;if(n>size)n=size;memset(b,0,n);return n;}
void tud_hid_set_report_cb(uint8_t i,uint8_t id,hid_report_type_t t,const uint8_t*b,uint16_t n){(void)i;(void)id;(void)t;(void)b;(void)n;}
const uint16_t*tud_descriptor_string_cb(uint8_t i,uint16_t lang){(void)lang;static uint16_t b[40];const char*s[]={"","STM32 Lab","STM32 Multi Tool","MULTI001"};if(!i){b[0]=0x304;b[1]=0x409;return b;}if(i>3)return 0;unsigned n=0;while(s[i][n]&&n<39){b[n+1]=s[i][n];++n;}b[0]=0x300+2*n+2;return b;}
