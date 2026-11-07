#include <interface.h>
#include "core/powerSave.h"
#include "core/utils.h"
#include <HatLink.h>

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
 static bool held=false;
 static int last_region=-1;
 static uint32_t repeat_at=0;
 HatLink::Touch t;
 if(!HatLink::touch(t)){held=false;touchPoint.pressed=false;return;}
 // Match touchHeatMap's six regions; select/back fire once per contact.
 const int col=t.x/(tftWidth/3),row=t.y/(tftHeight/3);
 const int region=col>=2?2:(col==0?(row==0?0:3):(t.y>tftHeight?4:(row==0?1:(row==1?4:5))));
 const uint32_t now=millis();
 const bool first=!held||region!=last_region;
 const bool navigation=region==1||region==2||region==3||region==5;
 if(!first&&(!navigation||(int32_t)(now-repeat_at)<0))return;
 held=true;last_region=region;repeat_at=now+(first?350:180);
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
