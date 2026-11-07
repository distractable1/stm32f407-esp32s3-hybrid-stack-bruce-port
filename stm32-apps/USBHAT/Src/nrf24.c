#include "nrf24.h"
#include <stdio.h>
#include <string.h>
static bool ready,scanning,bus_failed;
static unsigned channel,scan_channel;
static uint32_t scan_at,rx_count,tx_count,failures;
static uint8_t bins[126];
static bool bus(uint8_t cmd,const uint8_t *tx,uint8_t *rx,unsigned n){
 bool ok=nrf_hw_bus(cmd,tx,rx,n);if(!ok){bus_failed=true;failures++;}return ok;
}
static uint8_t rd(uint8_t reg){uint8_t v=255;bus(reg,0,&v,1);return v;}
static void wr(uint8_t reg,uint8_t v){bus(0x20|reg,&v,0,1);}
static void rx_mode(void){nrf_hw_ce(false);wr(5,76);channel=76;wr(0,0x0f);nrf_hw_ce(true);}
void nrf_stop(void){
 if(ready){nrf_hw_ce(false);wr(0,0x0c);}
 ready=false;scanning=false;nrf_hw_close();
}
bool nrf_start(void){
 nrf_stop();bus_failed=false;
 if(!nrf_hw_open())return false;
 nrf_hw_ce(false);
 // Reversible two-pattern register test rejects floating 00/FF SPI buses.
 uint8_t saved=rd(5);wr(5,42);uint8_t a=rd(5);wr(5,85);uint8_t b=rd(5);wr(5,saved);
 if(bus_failed||a!=42||b!=85){failures++;nrf_hw_close();return false;}
 wr(0,0x0c);wr(1,0);wr(2,1);wr(3,3);wr(4,0);wr(6,0); // CRC16, no auto-ACK, 1Mbps, -18dBm
 wr(0x1c,0);wr(0x1d,0);wr(0x11,32);
 const uint8_t address[5]={'U','S','B','H','1'};
 bus(0x2a,address,0,5);bus(0x30,address,0,5);
 bus(0xe1,0,0,0);bus(0xe2,0,0,0);wr(7,0x70);
 wr(0,0x0f);nrf_hw_delay(2000);
 ready=!bus_failed;
 if(!ready){nrf_hw_close();return false;}
 rx_mode();if(bus_failed){nrf_stop();return false;}return true;
}
bool nrf_tx(const uint8_t *data,unsigned n){
 if(!ready||scanning||!data||!n||n>32)return false;
 uint8_t padded[32]={0};memcpy(padded,data,n);bus_failed=false;
 nrf_hw_ce(false);wr(0,0x0e);wr(7,0x30);bus(0xe1,0,0,0);
 bus(0xa0,padded,0,32);nrf_hw_ce(true);nrf_hw_delay(15);nrf_hw_ce(false);
 bool sent=false;
 for(unsigned i=0;i<100;i++){uint8_t st=rd(7);if(st&0x20){sent=true;break;}if(st&0x10)break;nrf_hw_delay(20);}
 wr(7,0x30);if(!sent)bus(0xe1,0,0,0);rx_mode();
 if(sent&&!bus_failed){tx_count++;return true;}failures++;return false;
}
unsigned nrf_rx(uint8_t data[32]){
 if(!ready||scanning)return 0;
 bus_failed=false;
 if(rd(0x17)&1)return 0;
 bool ok=bus(0x61,0,data,32);wr(7,0x40);
 if(ok&&!bus_failed){rx_count++;return 32;}return 0;
}
bool nrf_scan_start(void){
 if(!ready||scanning)return false;
 bus_failed=false;memset(bins,0,sizeof bins);scan_channel=0;scan_at=UINT32_MAX;
 nrf_hw_ce(false);wr(0,0x0f);wr(5,0);nrf_hw_ce(true);
 if(bus_failed){nrf_stop();return false;}scanning=true;return true;
}
bool nrf_scan_tick(uint32_t now,uint8_t result[126]){
 if(!ready||!scanning)return false;
 if(scan_at==UINT32_MAX){scan_at=now;return false;}
 if((uint32_t)(now-scan_at)<1)return false; // >=1ms exceeds 130us+40us RPD settle
 nrf_hw_ce(false);bins[scan_channel]=rd(9)&1;
 if(bus_failed){nrf_stop();return false;}
 if(++scan_channel==126){memcpy(result,bins,126);scanning=false;bus(0xe2,0,0,0);wr(7,0x40);rx_mode();return true;}
 wr(5,scan_channel);nrf_hw_ce(true);scan_at=now;return false;
}
void nrf_info(char *out,size_t n){
 snprintf(out,n,"NRF24 %s; mode=%s; ch=%u; 1Mbps CRC16 addr=USBH1 fixed32 noAA; tx=%lu rx=%lu fail=%lu",
 ready?"DETECTED":"NOT DETECTED",!ready?"IDLE":(scanning?"SCAN":"RX"),channel,
 (unsigned long)tx_count,(unsigned long)rx_count,(unsigned long)failures);
}
