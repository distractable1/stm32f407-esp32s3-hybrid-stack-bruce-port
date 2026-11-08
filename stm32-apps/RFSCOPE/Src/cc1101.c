#include "cc1101.h"
#include "board_support.h"
#include "stm32f4xx.h"
static bool radio_open,spi_clock_was_on;
static uint32_t prior_cr1,prior_cr2;
static uint32_t pb_mode,pb_af,pb_speed,pb_pull,pg_mode,pg_pull,pg_out,pg_type,pb_type;
static void radio_restore(GPIO_TypeDef *g,uint32_t saved,uint32_t mask,volatile uint32_t *reg){(void)g;*reg=(*reg&~mask)|(saved&mask);}
bool cc_hw_open(void){
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
 GPIOG->BSRR=1u<<7;
 for(unsigned p=3;p<=5;p++){
  GPIOB->MODER=(GPIOB->MODER&~(3u<<(2*p)))|(2u<<(2*p));
  GPIOB->AFR[0]=(GPIOB->AFR[0]&~(15u<<(4*p)))|(6u<<(4*p));
  GPIOB->OSPEEDR|=3u<<(2*p);GPIOB->PUPDR&=~(3u<<(2*p));GPIOB->OTYPER&=~(1u<<p);
 }
 GPIOB->PUPDR=(GPIOB->PUPDR&~(3u<<8))|(1u<<8); // match reference scanner MISO pull-up
 GPIOG->MODER=(GPIOG->MODER&~(0x3fu<<12))|(1u<<14);
 GPIOG->OTYPER&=~((1u<<6)|(1u<<7));
 GPIOG->PUPDR=(GPIOG->PUPDR&~(0x3fu<<12))|(1u<<16);
 SPI3->CR2=0;SPI3->CR1=SPI_CR1_MSTR|SPI_CR1_SSM|SPI_CR1_SSI|(4u<<3)|SPI_CR1_SPE;
 radio_open=true;return true;
}

void cc_hw_delay(unsigned us){
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
bool cc_hw_bus(uint8_t cmd,const uint8_t *tx,uint8_t *rx,unsigned n){
 if(!radio_open||n>64)return false;
 GPIOG->BSRR=1u<<(7+16);
 unsigned ready=100000;while((GPIOB->IDR&(1u<<4))&&--ready){}
 uint8_t v;bool ok=ready && radio_byte(cmd,&v);
 for(unsigned i=0;i<n&&ok;i++){ok=radio_byte(tx?tx[i]:255,&v);if(rx)rx[i]=v;}
 unsigned budget=100000;while((SPI3->SR&SPI_SR_BSY)&&--budget){}if(!budget)ok=false;
 GPIOG->BSRR=1u<<7;return ok;
}
void cc_hw_close(void){
 if(!radio_open)return;
 GPIOG->BSRR=1u<<7;SPI3->CR1=prior_cr1;SPI3->CR2=prior_cr2;
 radio_restore(GPIOB,pb_mode,0xfc0,&GPIOB->MODER);radio_restore(GPIOB,pb_af,0xfff000,&GPIOB->AFR[0]);
 radio_restore(GPIOB,pb_speed,0xfc0,&GPIOB->OSPEEDR);radio_restore(GPIOB,pb_pull,0xfc0,&GPIOB->PUPDR);radio_restore(GPIOB,pb_type,0x38,&GPIOB->OTYPER);
 GPIOG->BSRR=(pg_out&0xc0)|((~pg_out&0xc0)<<16);
 radio_restore(GPIOG,pg_type,0xc0,&GPIOG->OTYPER);radio_restore(GPIOG,pg_pull,0x3f000,&GPIOG->PUPDR);radio_restore(GPIOG,pg_mode,0x3f000,&GPIOG->MODER);
 if(!spi_clock_was_on)RCC->APB1ENR&=~RCC_APB1ENR_SPI3EN;
 radio_open=false;
}

uint8_t cc_part,cc_version;
static bool identified;
static bool strobe(uint8_t c){return cc_hw_bus(c,0,0,0);}
static bool wr(uint8_t a,uint8_t v){return cc_hw_bus(a,&v,0,1);}
static bool rd(uint8_t a,uint8_t *v){return cc_hw_bus(a|0x80,0,v,1);}
static bool status(uint8_t a,uint8_t *v){return rd(a|0x40,v);}
static bool idle(void){
 if(!strobe(0x36))return false;
 for(unsigned i=0;i<200;i++){uint8_t state;if(!status(0x35,&state))return false;if((state&31)==1)return true;cc_hw_delay(10);}
 return false;
}
void cc_stop(void){if(identified){idle();strobe(0x3b);strobe(0x3a);strobe(0x34);}}
void cc_close(void){if(identified){idle();strobe(0x3b);strobe(0x3a);strobe(0x39);}identified=false;cc_hw_close();}
bool cc_init(uint32_t khz,unsigned power,bool ook){
 cc_close();if(!cc_hw_open())return false;
 GPIOG->BSRR=1u<<7;cc_hw_delay(50);
 GPIOG->BSRR=1u<<23;cc_hw_delay(10);GPIOG->BSRR=1u<<7;cc_hw_delay(50);
 if(!strobe(0x30))return false;
 cc_hw_delay(1000);
 if(!status(0x30,&cc_part)||!status(0x31,&cc_version)||cc_part!=0||cc_version==0||cc_version==255)return false;
 /* 26 MHz crystal, 38.383 kbps, 20.63 kHz deviation, 101.56 kHz BW.
  * Variable length, CRC, whitening, sync D391; RX stays RX, TX ends IDLE. */
 static const uint8_t profile[][2]={
 {0x00,0x2e},{0x01,0x2e},{0x02,0x06},{0x03,0x47},{0x04,0xd3},{0x05,0x91},
 {0x06,32},{0x07,0x04},{0x08,0x45},{0x09,0},{0x0a,0},{0x0b,0x06},{0x0c,0},
 {0x10,0xca},{0x11,0x83},{0x12,0x02},{0x13,0x22},{0x14,0xf8},{0x15,0x35},
 {0x16,0x07},{0x17,0x0c},{0x18,0x18},{0x19,0x16},{0x1a,0x6c},{0x1b,0x43},
 {0x1c,0x40},{0x1d,0x91},{0x21,0x56},{0x22,0x10},{0x23,0xe9},{0x24,0x2a},
 {0x25,0},{0x26,0x1f},{0x2c,0x81},{0x2d,0x35},{0x2e,0x09}};
 for(unsigned i=0;i<sizeof(profile)/sizeof(profile[0]);i++)if(!wr(profile[i][0],profile[i][1]))return false;
 if(ook && (!wr(0x12,0x32)||!wr(0x08,0x05)||!wr(0x22,0x11)))return false;
 uint32_t f=(uint32_t)(((uint64_t)khz*65536+13000)/26000);
 if(!wr(0x0d,f>>16)||!wr(0x0e,f>>8)||!wr(0x0f,f))return false;
 /* TI typical 433 MHz PA values: -30, -20, -15, -10 dBm. */
 static const uint8_t pa[]={0x12,0x0e,0x1d,0x34};
 uint8_t table[2]={ook?0:pa[power%4],pa[power%4]};
 if(!cc_hw_bus(0x7e,table,0,2))return false;
 uint8_t check;if(!rd(0x10,&check)||check!=0xca||!rd(0x0f,&check)||check!=(uint8_t)f)return false;
 identified=true;
 if(!strobe(0x34)){identified=false;return false;}
 return true;
}
bool cc_rssi(int *dbm){uint8_t v,s;if(!identified||!status(0x35,&s)||(s&31)!=13||!status(0x34,&v))return false;*dbm=((int)(int8_t)v)/2-74;return true;}
bool cc_send(uint32_t sequence){
 if(!identified||!idle()||!strobe(0x3b))return false;
 uint8_t p[17]={16,'R','F','S','C',sequence,sequence>>8,sequence>>16,sequence>>24,0x55,0xaa,0x55,0xaa,0x55,0xaa,0x55,0xaa};
 return cc_hw_bus(0x7f,p,0,sizeof p)&&strobe(0x35);
}
bool cc_poll(bool *done){uint8_t s,n;if(!status(0x35,&s)||!status(0x3a,&n)||(n&0x80))return false;*done=(s&31)==1 && n==0;if(*done)return strobe(0x34);return true;}
bool cc_receive(uint32_t *packets){
 uint8_t n;if(!identified||!status(0x3b,&n))return false;
 if(n&0x80){if(!idle()||!strobe(0x3a)||!strobe(0x34))return false;return true;}
 if(!(n&127))return true;
 uint8_t state;if(!status(0x38,&state))return false;
 if(state&8)return true; /* GDO sync asserted: do not drain a partial packet. */
 uint8_t len;if(!cc_hw_bus(0xff,0,&len,1))return false;
 if(len>32||n<len+3){cc_stop();return true;}
 uint8_t data[34];if(!cc_hw_bus(0xff,0,data,len+2))return false;
 if(data[len+1]&0x80)++*packets;
 return true;
}
