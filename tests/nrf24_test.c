#include "../stm32-apps/USBHAT/Src/nrf24.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t regs[32][5];static bool opened,floating,failed,ce,queued;static unsigned sent;
bool nrf_hw_open(void){opened=true;return true;}
void nrf_hw_close(void){opened=false;ce=false;}
void nrf_hw_delay(unsigned us){(void)us;}
void nrf_hw_ce(bool value){ce=value;if(value&&queued&&!(regs[0][0]&1)){regs[7][0]|=0x20;queued=false;sent++;}}
bool nrf_hw_bus(uint8_t cmd,const uint8_t *tx,uint8_t *rx,unsigned n){
 if(!opened||failed)return false;
 if(cmd==0xe2){regs[0x17][0]|=1;return true;}
 if(cmd==0xe1){queued=false;return true;}
 if(cmd==0xa0){assert(n==32);assert(tx[31]==0);queued=true;return true;}
 if(cmd==0x61){for(unsigned i=0;i<n;i++)rx[i]=i;regs[0x17][0]|=1;return true;}
 if(cmd<0x20){for(unsigned i=0;i<n;i++)rx[i]=floating?255:regs[cmd][i];return true;}
 if(cmd<0x40){unsigned r=cmd&31;for(unsigned i=0;i<n;i++){if(r==7)regs[r][i]&=~tx[i];else regs[r][i]=tx[i];}return true;}
 return false;
}
int main(void){
 uint8_t data[32]={1,2,3},bins[126];char info[200];
 assert(!nrf_tx(data,3));floating=true;assert(!nrf_start());assert(!opened);
 floating=false;assert(nrf_start());assert(opened&&ce);
 assert(regs[0][0]==15&&regs[1][0]==0&&regs[5][0]==76&&regs[6][0]==0&&regs[0x11][0]==32);
 assert(!memcmp(regs[0x0a],"USBH1",5)&&!memcmp(regs[0x10],"USBH1",5));
 assert(!nrf_tx(data,0)&&!nrf_tx(data,33));assert(nrf_tx(data,3));assert(sent==1&&ce&&regs[0][0]==15);
 assert(nrf_rx(data)==0);regs[0x17][0]=0;assert(nrf_rx(data)==32);for(unsigned i=0;i<32;i++)assert(data[i]==i);
 assert(nrf_scan_start());assert(!nrf_scan_start());assert(!nrf_tx(data,1));assert(!nrf_scan_tick(0,bins));
 for(unsigned i=0;i<126;i++){regs[9][0]=i&1;bool done=nrf_scan_tick(i+1,bins);assert(done==(i==125));}
 for(unsigned i=0;i<126;i++)assert(bins[i]==(i&1));
 assert(sent==1&&regs[5][0]==76&&ce); // scanning never transmits
 assert(nrf_scan_start());assert(!nrf_scan_tick(500,bins));failed=true;assert(!nrf_scan_tick(501,bins));
 nrf_stop();assert(!opened&&!ce);failed=false;
 nrf_info(info,sizeof info);assert(strstr(info,"NOT DETECTED"));
 puts("PASS: missing-radio rejection, register profile, TX bounds/padding, FIFO RX, 126-channel passive scan, RX restoration, bus failure, teardown");
}
