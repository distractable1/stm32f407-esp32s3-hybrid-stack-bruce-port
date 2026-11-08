#include "nrf_radio.h"
#define REG(a) (*(volatile uint32_t *)(a))
#define B 0x40020400u
#define G 0x40021800u
static unsigned dynamic;
static unsigned fixed_length;
static void pin(unsigned port, unsigned number, unsigned high)
{
    REG(port+0x18)=1u<<(number+(high ? 0 : 16));
}
static void mode(unsigned port,unsigned pin,unsigned value)
{
    unsigned shift=2*pin;
    REG(port)=(REG(port)&~(3u<<shift))|(value<<shift);
}
static void wait_us(unsigned us)
{
    uint32_t start=REG(0xe0001004u);
    while((uint32_t)(REG(0xe0001004u)-start)<us*16u) {}
}
static uint8_t spi(uint8_t value)
{
    uint8_t result=0;
    for(unsigned mask=128;mask;mask>>=1){
        pin(B,5,value&mask);
        pin(B,3,1);
        result=(result<<1)|((REG(B+0x10)>>4)&1);
        pin(B,3,0);
    }
    return result;
}
static uint8_t read_reg(unsigned address)
{
    pin(G,7,0); spi(address&31);
    uint8_t value=spi(255); pin(G,7,1);
    return value;
}
static void write_reg(unsigned address,uint8_t value)
{
    pin(G,7,0); spi(0x20|(address&31)); spi(value); pin(G,7,1);
}
static void instruction(unsigned command)
{
    pin(G,7,0);spi(command);pin(G,7,1);
}
void radio_stop(void)
{
    pin(G,6,0);
    write_reg(0,read_reg(0)&~2u);
}
int radio_init(void)
{
    REG(0xe000edfc)|=1u<<24;REG(0xe0001000)|=1;
    REG(0x40023830u)|=(1u<<1)|(1u<<6);
    /* PB14 is the shared SPI flash's chip select. Keep flash deselected. */
    pin(B,14,1);mode(B,14,1);
    pin(G,6,0);pin(G,7,1);pin(B,3,0);
    mode(G,6,1);mode(G,7,1);mode(B,3,1);mode(B,5,1);mode(B,4,0);mode(G,8,0);
    wait_us(5000);
    unsigned width=read_reg(3);
    if(width<1||width>3)return 0;
    unsigned old_channel=read_reg(5);
    write_reg(5,42);
    unsigned good=read_reg(5)==42;
    write_reg(5,old_channel);
    if(!good)return 0;
    write_reg(0,0x0c);write_reg(1,0);write_reg(2,1);write_reg(4,0);
    write_reg(0x1c,0);write_reg(0x1d,0);write_reg(7,0x70);
    instruction(0xe1);instruction(0xe2);
    return 1;
}
int radio_receive(const RadioProfile *p)
{
    if(p->channel>83||p->rate>2||p->address_length<3||p->address_length>5||
       p->payload_length<1||p->payload_length>32||p->crc_bytes>2)return 0;
    pin(G,6,0);
    write_reg(0,0x0c);write_reg(1,0);write_reg(2,1);
    write_reg(3,p->address_length-2);write_reg(4,0);write_reg(5,p->channel);
    unsigned setup=6|(p->rate==0 ? 0x20 : p->rate==2 ? 8 : 0);
    write_reg(6,setup);
    pin(G,7,0);spi(0x2a);
    for(unsigned i=0;i<p->address_length;++i)spi(p->address[i]);
    pin(G,7,1);
    write_reg(0x11,p->payload_length);
    dynamic=p->dynamic_payload;
    fixed_length=p->payload_length;
    write_reg(0x1d,dynamic ? 4 : 0);
    if(dynamic && !(read_reg(0x1d)&4)){
        pin(G,7,0);spi(0x50);spi(0x73);pin(G,7,1);
        write_reg(0x1d,4);
    }
    if(dynamic && !(read_reg(0x1d)&4))return 0;
    write_reg(0x1c,dynamic ? 1 : 0);
    write_reg(7,0x70);instruction(0xe2);
    unsigned crc=p->crc_bytes==0 ? 0 : p->crc_bytes==1 ? 8 : 12;
    write_reg(0,crc|3);
    wait_us(1600);pin(G,6,1);wait_us(150);
    return 1;
}
int radio_read(uint8_t payload[32],unsigned *length)
{
    if(read_reg(0x17)&1)return 0;
    unsigned size=fixed_length;
    if(dynamic){
        pin(G,7,0);spi(0x60);size=spi(255);pin(G,7,1);
        if(size>32){instruction(0xe2);write_reg(7,0x40);return -1;}
    }
    pin(G,7,0);spi(0x61);
    for(unsigned i=0;i<size;++i)payload[i]=spi(255);
    pin(G,7,1);
    write_reg(7,0x40);*length=size;
    return 1;
}
unsigned radio_activity(unsigned channel)
{
    if(channel>83)return 0;
    pin(G,6,0);write_reg(0,3);write_reg(1,0);
    write_reg(5,channel);write_reg(6,6);
    wait_us(1600);pin(G,6,1);wait_us(200);
    unsigned activity=read_reg(9)&1;
    pin(G,6,0);
    return activity;
}

int radio_send_once(const RadioProfile *profile, const uint8_t *payload, unsigned length)
{
    if(length>32 || (!profile->dynamic_payload && length!=profile->payload_length))return 0;
    if(!radio_receive(profile))return 0;
    pin(G,6,0);
    write_reg(0,read_reg(0)&~1u);
    write_reg(6,read_reg(6)&~6u); /* Minimum configured RF output. */
    pin(G,7,0);spi(0x30);
    for(unsigned i=0;i<profile->address_length;++i)spi(profile->address[i]);
    pin(G,7,1);
    instruction(0xe1);write_reg(7,0x70);
    pin(G,7,0);spi(0xa0);
    for(unsigned i=0;i<length;++i)spi(payload[i]);
    pin(G,7,1);
    pin(G,6,1);wait_us(20);pin(G,6,0);
    unsigned sent=0;
    for(unsigned i=0;i<500;++i){
        unsigned status=read_reg(7);
        if(status&0x20){sent=1;break;}
        if(status&0x10)break;
        wait_us(100);
    }
    instruction(0xe1);write_reg(7,0x70);
    radio_receive(profile);
    return sent;
}
