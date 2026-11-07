#include "tusb.h"
#include "hat_protocol.h"
static const tusb_desc_device_t device = {
 .bLength=sizeof(tusb_desc_device_t), .bDescriptorType=TUSB_DESC_DEVICE,
 .bcdUSB=0x0200, .bDeviceClass=TUSB_CLASS_MISC,
 .bDeviceSubClass=MISC_SUBCLASS_COMMON, .bDeviceProtocol=MISC_PROTOCOL_IAD,
 .bMaxPacketSize0=64, .idVendor=HAT_VID, .idProduct=HAT_PID,
 .bcdDevice=0x0100, .iManufacturer=1, .iProduct=2, .iSerialNumber=3,
 .bNumConfigurations=1
};
static const uint8_t config[]={
 TUD_CONFIG_DESCRIPTOR(1,2,0,TUD_CONFIG_DESC_LEN+TUD_CDC_DESC_LEN,0,100),
 TUD_CDC_DESCRIPTOR(0,4,0x81,8,0x02,0x82,64)
};
uint8_t const *tud_descriptor_device_cb(void){return (const uint8_t *)&device;}
uint8_t const *tud_descriptor_configuration_cb(uint8_t index){return index?NULL:config;}
uint16_t const *tud_descriptor_string_cb(uint8_t index,uint16_t langid){
 (void)langid; static uint16_t result[48];
 static const char *strings[]={"","Bench","T100 USB Peripheral","USBHAT-01","Multiplexed CDC"};
 if(index==0){result[0]=(TUSB_DESC_STRING<<8)|4; result[1]=0x0409; return result;}
 if(index>=sizeof strings/sizeof *strings)return NULL;
 unsigned n=0; while(strings[index][n] && n<47){result[n+1]=(uint8_t)strings[index][n];n++;}
 result[0]=(TUSB_DESC_STRING<<8)|(2*n+2);return result;
}
