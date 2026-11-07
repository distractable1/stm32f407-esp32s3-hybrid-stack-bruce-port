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

namespace HatLink {uint32_t scanSnapshot(uint8_t *bins);void radioDiagnostic(char *,size_t);}
static bool usbhat_detect(){
 if(HatLink::selectRadio(2))return true;
 vTaskDelay(pdMS_TO_TICKS(100));
 char error[80];HatLink::radioDiagnostic(error,sizeof error);displayError(error);return false;
}
void usbhat_radio_pins(){
 options={
  {"Transport: STM32 USB bridge",[]{displayInfo("ESP32 radio GPIOs unused",true);}},
  {"SCK PB3 / MISO PB4 / MOSI PB5",[]{displayInfo("Fixed STM32 SPI3 pins",true);}},
  {"CS PG7 / CE PG6 / IRQ PG8",[]{displayInfo("Fixed STM32 socket pins",true);}},
  {"Detect remote NRF24",[]{if(usbhat_detect())displayInfo("STM32 NRF24 detected");}},
 };
 addOptionToMainMenu();loopOptions(options,MENU_TYPE_SUBMENU,"STM32 Radio Pins");
}
static void usbhat_scan(){
 if(!usbhat_detect())return;
 uint8_t bins[126];uint32_t revision=HatLink::scanSnapshot(bins);
 tft.fillScreen(bruceConfig.bgColor);tft.setTextSize(1);
 tft.drawString("NRF24 energy scan (RX only)",8,8);
 tft.drawString("2.400 - 2.525GHz; threshold bits",8,23);
 tft.drawString("Tap Back to exit",8,142);
 if(!HatLink::command("SYS:NRFSCAN")){displayError("Scan failed");return;}
 uint32_t requested=millis();
 while(!check(EscPress)&&HatLink::connected()){
  const uint32_t next=HatLink::scanSnapshot(bins);
  if(next!=revision){
   revision=next;
   tft.fillRect(8,42,304,90,bruceConfig.bgColor);
   for(unsigned i=0;i<126;i++)tft.fillRect(10+i*2,130-(bins[i]?75:2),2,bins[i]?75:2,bruceConfig.priColor);
   tft.drawString("2400            2463           2525 MHz",8,132);
   if(!HatLink::command("SYS:NRFSCAN"))break;
   requested=millis();
  }
  if((uint32_t)(millis()-requested)>3000){displayError("NRF scan timeout");break;}
  vTaskDelay(pdMS_TO_TICKS(20));
 }
 HatLink::command("SYS:NRFOFF!");
}
void usbhat_bench_menu(){
 options={
  {"Link status (UART)",[]{HatLink::status();}},
  {"NRF24 detect / start RX",[]{
    if(usbhat_detect()){HatLink::command("SYS:NRFINFO");displayInfo("NRF24 RX ch76; info on UART");}

  }},
  {"NRF24 energy scanner",usbhat_scan},
  {"NRF24 send bench packet",[]{
    const uint8_t p[]={'U','S','B','H','A','T',0xaa,0x7e,0x7d,0,0xff};
    if(usbhat_detect()&&HatLink::radioTransmit(p,sizeof p))displayInfo("Packet sent; peer RX not verified");
    else displayError("NRF24 TX failed");
  }},
  {"Radio off",[]{HatLink::command("SYS:NRFOFF!");}},
  {"CC1101 (not implemented)",[]{displayError("CC1101 driver pending");}},
  {"STM32 launcher",[]{HatLink::command("SYS:LAUNCHER");}},
 };
 addOptionToMainMenu();loopOptions(options,MENU_TYPE_SUBMENU,"USB HAT Radio");
}
