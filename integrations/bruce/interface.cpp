#include <interface.h>
#include "core/powerSave.h"
#include "core/utils.h"
#include <HatLink.h>
#include <hat_contact.h>

static void usbhat_diagnostics(void *){
 for(;;){
  if(HatLink::connected())HatLink::status();
  vTaskDelay(pdMS_TO_TICKS(5000));
 }
}
void _setup_gpio(){
 bruceConfig.colorInverted=0;
 if(!HatLink::begin())Serial.println("[USBHAT] FATAL: USB host startup failed");
 else if(xTaskCreate(usbhat_diagnostics,"hat-diag",4096,nullptr,1,nullptr)!=pdPASS)
  Serial.println("[USBHAT] periodic diagnostics unavailable (allocation failed)");
}
int getBattery(){return 0;}
bool isCharging(){return false;}
void _setBrightness(uint8_t value){(void)value;}
void InputHandler(){
 static hat_contact contact={};
 HatLink::Touch t;
 const bool pressed=HatLink::touch(t);
 touchPoint.pressed=false;
 if(!hat_contact_step(&contact,pressed,t.at,millis()))return;
 Serial.printf("[USBHAT] TOUCH action x=%u y=%u age=%lu ms\n",t.x,t.y,(unsigned long)(millis()-t.at));
 if(wakeUpScreen())return;
 AnyKeyPress=true;touchPoint.x=t.x;touchPoint.y=t.y;touchPoint.pressed=true;
 touchHeatMap(touchPoint);
}

void powerOff(){HatLink::command("SYS:LAUNCHER");}
void checkReboot(){}

void usbhat_bench_menu(){
 options={
  {"Link status (UART)",[]{HatLink::status();}},
  {"CC1101 mock loopback",[]{
    const uint8_t p[]={0xaa,0x7e,0x7d,0,0xff};
    if(HatLink::selectRadio(1))HatLink::radioTransmit(p,sizeof p);
  }},
  {"NRF24 mock loopback",[]{
    const uint8_t p[]={0xaa,0x7e,0x7d,0,0xff};
    if(HatLink::selectRadio(2))HatLink::radioTransmit(p,sizeof p);
  }},
  {"STM32 launcher",[]{HatLink::command("SYS:LAUNCHER");}},
 };
 addOptionToMainMenu();loopOptions(options,MENU_TYPE_SUBMENU,"USB HAT Bench");
}
