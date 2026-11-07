#include "hat_board.h"
#include "board_support.h"
#include "stm32f4xx.h"
#include "tusb.h"
#include "hat_window.h"
bool hat_usb_start(void){
 RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
 RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
 (void)RCC->AHB2ENR;
 for(unsigned pin=11;pin<=12;pin++){
  GPIOA->MODER=(GPIOA->MODER&~(3u<<(pin*2)))|(2u<<(pin*2));
  GPIOA->OTYPER &= ~(1u<<pin); GPIOA->OSPEEDR |= 3u<<(pin*2);
  GPIOA->PUPDR &= ~(3u<<(pin*2));
  GPIOA->AFR[1]=(GPIOA->AFR[1]&~(15u<<((pin-8)*4)))|(10u<<((pin-8)*4));
 }
 NVIC_SetPriority(OTG_FS_IRQn,5);
 tusb_rhport_init_t init={.role=TUSB_ROLE_DEVICE,.speed=TUSB_SPEED_FULL};
 return tusb_init(0,&init);
}
void OTG_FS_IRQHandler(void){tud_int_handler(0);}
void hat_usb_stop(void){
 tud_disconnect(); tud_deinit(0);
 NVIC_DisableIRQ(OTG_FS_IRQn);NVIC_ClearPendingIRQ(OTG_FS_IRQn);
 RCC->AHB2ENR &= ~RCC_AHB2ENR_OTGFSEN;
}
/* Documented T100 LCD bus. Do not change inherited GPIO modes or clocks. */
static uint32_t data_words,last_pixel_words;
uint32_t Display_Last_Pixel_Words(void){return last_pixel_words;}
static void lcd_word(void *ctx,uint16_t word,bool data){
 (void)ctx;
 GPIOF->BSRR=1u<<(12+(data?0:16));
 uint32_t pd=(word&3u)<<14 | (word>>2&3u) | (word>>13&7u)<<8;
 uint32_t pe=(word&0x1ff0u)<<3;
 GPIOD->BSRR=((0xc703u&~pd)<<16)|pd;GPIOE->BSRR=((0xff80u&~pe)<<16)|pe;
 GPIOD->BSRR=1u<<(5+16);
 for(volatile unsigned i=0;i<8;i++)__NOP();
 GPIOD->BSRR=1u<<5;
 if(data)++data_words;
}
bool Display_Flush_Window(uint16_t x,uint16_t y,uint16_t w,uint16_t h,const uint8_t *p){
 hat_window v;
 last_pixel_words=0;
 if(!p||!hat_window_bounds(&v,x,y,w,h))return false;
 /* One selected RAMWR transaction; do not terminate the pixel stream after
  * every word. The protected SDK remains unchanged. */
 GPIOG->BSRR=1u<<(12+16);
 data_words=0;
 uint32_t written=hat_lcd_emit(&v,p,lcd_word,NULL);
 GPIOG->BSRR=1u<<12;
 last_pixel_words=data_words>=8?data_words-8:0;
 return written==(uint32_t)w*h&&last_pixel_words==written;
}
/* Mock only; no unverified RF socket pins are driven. */
bool Radio_SPI_Transmit(uint8_t mode,const uint8_t *data,uint16_t length){
 (void)data;return (mode==1&&length<=61)||(mode==2&&length<=32);
}
