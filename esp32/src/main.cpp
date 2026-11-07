#include <Arduino.h>
#include <HatLink.h>
#ifdef HAT_DISPLAY_TEST
#include <HatDisplay.h>
static HatPanel panel;
static lgfx::LGFX_Device screen;
#endif
void setup(){
 Serial.begin(115200);delay(500);
 Serial.println("USB HAT bench: UART console; native USB is HOST");
 if(!HatLink::begin()){Serial.println("FATAL: host initialization failed");for(;;)delay(1000);}
#ifdef HAT_DISPLAY_TEST
 screen.setPanel(&panel);
 if(!screen.init()){Serial.println("FATAL: framebuffer initialization failed");for(;;)delay(1000);}
 screen.fillScreen(0);screen.setTextColor(0xffff);screen.drawString("USB HAT framebuffer",8,8);
#endif
}
void loop(){
 static uint32_t display_at,status_at;static uint16_t color;
 if(HatLink::connected()){
  if(millis()-display_at>=1000){
   display_at=millis();uint8_t pixels[16*16*2];color+=0x841;
   for(unsigned i=0;i<sizeof pixels;i+=2)hat_put16(pixels+i,color);
#ifdef HAT_DISPLAY_TEST
   screen.fillRect(32,32,16,16,color);
#else
   Serial.printf("[BENCH] window ACK=%d\n",HatLink::window(32,32,16,16,pixels));
#endif
  }
  if(millis()-status_at>=5000){status_at=millis();HatLink::status();}
 }
 static uint32_t touch_at;HatLink::Touch t;
 if(HatLink::touch(t)&&t.at!=touch_at){touch_at=t.at;Serial.printf("[BENCH] touch %u,%u\n",t.x,t.y);}
 if(Serial.available()){
  char c=Serial.read();if(c=='s')HatLink::status();if(c=='L')HatLink::command("SYS:LAUNCHER");
  if(c=='n')Serial.printf("NRF24 detection=%d\n",HatLink::selectRadio(2));
  if(c=='c')HatLink::command("SYS:NRFSCAN");
  if(c=='o')HatLink::command("SYS:NRFOFF!");
  if(c=='t'){const uint8_t p[]={'U','S','B','H','A','T'};Serial.printf("NRF24 TX=%d\n",HatLink::radioTransmit(p,sizeof p));}
 }
 delay(5);
}
