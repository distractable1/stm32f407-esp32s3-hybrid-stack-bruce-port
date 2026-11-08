#include "tool_radio.h"
#include "board_support.h"
#include "stm32f4xx.h"
#include <string.h>
#include <limits.h>
#include <stdio.h>
static bool bus_error;static char radio_error[64];
static void wait_us(unsigned us){CoreDebug->DEMCR|=CoreDebug_DEMCR_TRCENA_Msk;DWT->CTRL|=1;uint32_t start=DWT->CYCCNT;while((uint32_t)(DWT->CYCCNT-start)<(SystemCoreClock/1000000u)*us){}}
static RadioProfile profile;static bool present,scan,receive;static uint8_t bins[126];static unsigned position;static unsigned channels=126;static uint32_t deadline;static bool settling;static const char*status="Radio off";
static void pin(GPIO_TypeDef*p,unsigned n,unsigned v){p->BSRR=1u<<(n+(v?0:16));}
static void mode(GPIO_TypeDef*p,unsigned n,unsigned v){p->MODER=(p->MODER&~(3u<<(n*2)))|(v<<(n*2));}
static uint8_t spi(uint8_t b){uint32_t t=tusb_time_millis_api();while(!(SPI3->SR&SPI_SR_TXE))if((uint32_t)(tusb_time_millis_api()-t)>5)return 255;*(volatile uint8_t*)&SPI3->DR=b;while(!(SPI3->SR&SPI_SR_RXNE))if((uint32_t)(tusb_time_millis_api()-t)>5)return 255;return *(volatile uint8_t*)&SPI3->DR;}
static bool cc_ready(void){uint32_t start=tusb_time_millis_api();while(GPIOB->IDR&(1u<<4)){if((uint32_t)(tusb_time_millis_api()-start)>=10){bus_error=true;return false;}}return true;}
static void select(void){pin(GPIOG,7,0);if(profile.family==RADIO_CC1101)cc_ready();}
static void end(void){uint32_t start=tusb_time_millis_api();while(SPI3->SR&SPI_SR_BSY){if((uint32_t)(tusb_time_millis_api()-start)>=5){bus_error=true;break;}}pin(GPIOG,7,1);}
static bool cc_reset(void){pin(GPIOG,7,1);wait_us(50);pin(GPIOG,7,0);wait_us(50);pin(GPIOG,7,1);wait_us(50);select();if(bus_error){end();return false;}spi(0x30);if(!cc_ready()){end();return false;}end();wait_us(100);return true;}
static uint8_t readreg(uint8_t r){select();spi(profile.family==RADIO_CC1101?r|0xc0:r);uint8_t v=spi(255);end();return v;}
static void write(uint8_t r,uint8_t v){select();spi(profile.family==RADIO_CC1101?r:r|0x20);spi(v);end();}
static void strobe(uint8_t s){select();spi(s);end();}
static void tune(void){if(profile.family==RADIO_NRF24){write(5,position);pin(GPIOG,6,1);}else{uint32_t center=profile.frequency_khz;int32_t f=(int32_t)center+((int)position-15)*100;uint32_t word=(uint32_t)(((uint64_t)f*65536)/26000);strobe(0x36);write(0x0d,word>>16);write(0x0e,word>>8);write(0x0f,word);strobe(0x33);strobe(0x34);}}
void tool_radio_defaults(ToolRadio f,RadioProfile*p){memset(p,0,sizeof(*p));p->family=f;p->channel=76;p->payload_length=32;p->crc_bytes=2;p->frequency_khz=f==RADIO_CC1101?433920:2476000;p->rate_bps=f==RADIO_CC1101?2400:1000000;memset(p->address,0xe7,5);}
void tool_radio_stop(void){scan=receive=false;settling=false;status=present?"Scan stopped":"Radio off";if(present){if(profile.family==RADIO_NRF24)pin(GPIOG,6,0);else strobe(0x36);}}
void tool_radio_close(void){tool_radio_stop();if(present&&profile.family==RADIO_NRF24)write(0,0x0c);present=false;status="Radio off";}
bool tool_radio_open(const RadioProfile*p){tool_radio_close();profile=*p;bus_error=false;if(p->family==RADIO_OFF)return false;if(p->family==RADIO_CC1101 && !((p->frequency_khz>=301500&&p->frequency_khz<=346500)||(p->frequency_khz>=388500&&p->frequency_khz<=462500)||(p->frequency_khz>=780500&&p->frequency_khz<=926500))){status="CC scan exceeds supported band";return false;}RCC->AHB1ENR|=(1u<<1)|(1u<<6);RCC->APB1ENR|=RCC_APB1ENR_SPI3EN;pin(GPIOB,14,1);mode(GPIOB,14,1);pin(GPIOG,7,1);mode(GPIOG,7,1);mode(GPIOG,6,0);mode(GPIOG,8,0);for(unsigned i=3;i<=5;++i){mode(GPIOB,i,2);GPIOB->AFR[0]=(GPIOB->AFR[0]&~(15u<<(i*4)))|(6u<<(i*4));}GPIOB->PUPDR=(GPIOB->PUPDR&~(3u<<8))|(1u<<8);SPI3->CR1&=~SPI_CR1_SPE;SPI3->CR1=SPI_CR1_MSTR|SPI_CR1_SSM|SPI_CR1_SSI|SPI_CR1_BR_1|SPI_CR1_BR_0|SPI_CR1_SPE;
if(p->family==RADIO_NRF24){if(p->channel>125||p->payload_length<1||p->payload_length>32){status="Invalid nRF profile";return false;}if(readreg(3)!=3){status="nRF24 not detected";return false;}pin(GPIOG,6,0);mode(GPIOG,6,1);write(1,0);write(2,1);write(3,3);write(4,0);write(5,p->channel);write(6,p->rate_bps==250000?0x20:p->rate_bps==2000000?8:0);select();spi(0x2a);for(unsigned i=0;i<5;++i)spi(p->address[i]);end();write(0x11,p->payload_length);write(0x1c,0);write(0x1d,0);write(7,0x70);strobe(0xe2);write(0,p->crc_bytes==1?0x0b:0x0f);deadline=tusb_time_millis_api()+5;}
else {if(!cc_reset()){status="CC SO-ready timeout (check wiring)";return false;}uint8_t part=readreg(0x30),version=readreg(0x31);if(bus_error||part!=0||version==0||version==255){snprintf(radio_error,sizeof radio_error,"CC PART=%02X VER=%02X",part,version);status=radio_error;return false;}write(0,0x2e);write(2,0x2e);write(0x10,0xf6);write(0x11,0x83);write(0x12,0x13);write(0x15,0x15);write(0x18,0x18);write(0x1b,0x43);write(0x1c,0x40);write(0x1d,0x91);}
if(bus_error){status="Radio SPI timeout";return false;}present=true;status="Radio connected";return true;}
bool tool_radio_scan_start(void){if(!present)return false;tool_radio_stop();memset(bins,0,sizeof(bins));position=0;scan=true;status=profile.family==RADIO_NRF24?"nRF occupancy (RPD)":"CC1101 RSSI sweep";return true;}
bool tool_radio_receive_start(void){status="Packet RX not in this preview";return false;}
void tool_radio_tick(uint32_t now){if(!scan||!present)return;if(!settling){if((int32_t)(now-deadline)<0)return;tune();settling=true;deadline=now+(profile.family==RADIO_NRF24?2:15);return;}if((int32_t)(now-deadline)<0)return;int value;if(profile.family==RADIO_NRF24){value=readreg(9)?100:0;pin(GPIOG,6,0);}else{int r=(int8_t)readreg(0x34);int dbm=r/2-74;value=(dbm+110)*100/80;if(value<0)value=0;if(value>100)value=100;strobe(0x36);}bins[position]=(bins[position]*3+value)/4;position=(position+1)%(profile.family==RADIO_NRF24?channels:31);settling=false;}
bool tool_radio_packet(RadioPacket*p){(void)p;return false;}const uint8_t*tool_radio_bins(unsigned*n){*n=profile.family==RADIO_CC1101?31:channels;return bins;}unsigned tool_radio_scan_position(void){return position;}const char*tool_radio_status(void){return status;}bool tool_radio_present(void){return present;}bool tool_radio_scanning(void){return scan;}bool tool_radio_receiving(void){return receive;}uint32_t tool_radio_drops(void){return 0;}

void tool_radio_set_channels(unsigned n){if(n==84||n==126){tool_radio_stop();channels=n;position=0;memset(bins,0,sizeof bins);}}
