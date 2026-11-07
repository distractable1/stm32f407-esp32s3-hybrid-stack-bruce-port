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
#include "nrf24.h"
static bool radio_open,spi_clock_was_on;
static uint32_t prior_cr1,prior_cr2;
static uint32_t pb_mode,pb_af,pb_speed,pb_pull,pg_mode,pg_pull,pg_out,pg_type,pb_type;
static void radio_restore(GPIO_TypeDef *g,uint32_t saved,uint32_t mask,volatile uint32_t *reg){(void)g;*reg=(*reg&~mask)|(saved&mask);}
bool nrf_hw_open(void){
 if(radio_open)return true;
 RCC->AHB1ENR|=RCC_AHB1ENR_GPIOBEN|RCC_AHB1ENR_GPIOGEN;
 spi_clock_was_on=(RCC->APB1ENR&RCC_APB1ENR_SPI3EN)!=0;
 RCC->APB1ENR|=RCC_APB1ENR_SPI3EN;(void)RCC->APB1ENR;
 // The protected SDK does not use SPI3; a previous launcher app can leave
 // SPE set. Adopt an idle inherited controller instead of rejecting it.
 unsigned budget=100000;while((SPI3->SR&SPI_SR_BSY)&&--budget){}
 if(!budget){if(!spi_clock_was_on)RCC->APB1ENR&=~RCC_APB1ENR_SPI3EN;return false;}
 prior_cr1=SPI3->CR1;prior_cr2=SPI3->CR2;
 SPI3->CR1&=~SPI_CR1_SPE;
 (void)SPI3->DR;(void)SPI3->SR;
 pb_mode=GPIOB->MODER;pb_af=GPIOB->AFR[0];pb_speed=GPIOB->OSPEEDR;pb_pull=GPIOB->PUPDR;pb_type=GPIOB->OTYPER;
 pg_mode=GPIOG->MODER;pg_pull=GPIOG->PUPDR;pg_out=GPIOG->ODR;pg_type=GPIOG->OTYPER;
 GPIOG->BSRR=(1u<<7)|(1u<<(6+16));
 for(unsigned p=3;p<=5;p++){
  GPIOB->MODER=(GPIOB->MODER&~(3u<<(2*p)))|(2u<<(2*p));
  GPIOB->AFR[0]=(GPIOB->AFR[0]&~(15u<<(4*p)))|(6u<<(4*p));
  GPIOB->OSPEEDR|=3u<<(2*p);GPIOB->PUPDR&=~(3u<<(2*p));GPIOB->OTYPER&=~(1u<<p);
 }
 GPIOB->PUPDR=(GPIOB->PUPDR&~(3u<<8))|(1u<<8); // match reference scanner MISO pull-up
 GPIOG->MODER=(GPIOG->MODER&~(0x3fu<<12))|(1u<<12)|(1u<<14);
 GPIOG->OTYPER&=~((1u<<6)|(1u<<7));
 GPIOG->PUPDR=(GPIOG->PUPDR&~(0x3fu<<12))|(1u<<16);
 SPI3->CR2=0;SPI3->CR1=SPI_CR1_MSTR|SPI_CR1_SSM|SPI_CR1_SSI|(4u<<3)|SPI_CR1_SPE;
 radio_open=true;return true;
}
void nrf_hw_ce(bool high){GPIOG->BSRR=1u<<(6+(high?0:16));}
void nrf_hw_delay(unsigned us){
 CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CTRL|=DWT_CTRL_CYCCNTENA_Msk;
 uint32_t start=DWT->CYCCNT,cycles=(SystemCoreClock/1000000u)*us;
 while((uint32_t)(DWT->CYCCNT-start)<cycles){}
}
static bool radio_byte(uint8_t out,uint8_t *in){
 unsigned budget=100000;while(!(SPI3->SR&SPI_SR_TXE)&&--budget){}if(!budget)return false;
 *(__IO uint8_t *)&SPI3->DR=out;budget=100000;
 while(!(SPI3->SR&SPI_SR_RXNE)&&--budget){}if(!budget)return false;
 *in=*(__IO uint8_t *)&SPI3->DR;return true;
}
bool nrf_hw_bus(uint8_t cmd,const uint8_t *tx,uint8_t *rx,unsigned n){
 if(!radio_open||n>32)return false;
 GPIOG->BSRR=1u<<(7+16);uint8_t v;bool ok=radio_byte(cmd,&v);
 for(unsigned i=0;i<n&&ok;i++){ok=radio_byte(tx?tx[i]:255,&v);if(rx)rx[i]=v;}
 unsigned budget=100000;while((SPI3->SR&SPI_SR_BSY)&&--budget){}if(!budget)ok=false;
 GPIOG->BSRR=1u<<7;return ok;
}
void nrf_hw_close(void){
 if(!radio_open)return;
 nrf_hw_ce(false);GPIOG->BSRR=1u<<7;SPI3->CR1=prior_cr1;SPI3->CR2=prior_cr2;
 radio_restore(GPIOB,pb_mode,0xfc0,&GPIOB->MODER);radio_restore(GPIOB,pb_af,0xfff000,&GPIOB->AFR[0]);
 radio_restore(GPIOB,pb_speed,0xfc0,&GPIOB->OSPEEDR);radio_restore(GPIOB,pb_pull,0xfc0,&GPIOB->PUPDR);radio_restore(GPIOB,pb_type,0x38,&GPIOB->OTYPER);
 GPIOG->BSRR=(pg_out&0xc0)|((~pg_out&0xc0)<<16);
 radio_restore(GPIOG,pg_type,0xc0,&GPIOG->OTYPER);radio_restore(GPIOG,pg_pull,0x3f000,&GPIOG->PUPDR);radio_restore(GPIOG,pg_mode,0x3f000,&GPIOG->MODER);
 if(!spi_clock_was_on)RCC->APB1ENR&=~RCC_APB1ENR_SPI3EN;
 radio_open=false;
}
bool Radio_SPI_Transmit(uint8_t mode,const uint8_t *data,uint16_t length){return mode==2&&nrf_tx(data,length);}
