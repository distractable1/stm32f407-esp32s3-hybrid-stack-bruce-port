#include "synth_usb.h"
#include "tusb.h"
#include "stm32f4xx.h"
#include <math.h>
#include <string.h>
#include "engine_usb.h"
#include "synthesizer_parameters.h"

uint32_t SystemCoreClock=16000000;
static volatile uint32_t ticks;
bool synth_usb_initialized;
volatile uint32_t synth_frames_in,synth_frames_out,synth_sample_count;
volatile bool synth_recording,synth_playing,synth_tone;
volatile unsigned synth_wave,synth_volume=40;
volatile bool synth_input_active,synth_output_active;
static int16_t sine_table[256];

static int16_t sample_memory[24000];
static uint32_t sample_position,tone_phase;
static int16_t engine_ring[768];
static volatile uint32_t engine_write,engine_read;
static volatile uint32_t engine_underruns;

void synth_audio_task(void)
{
    if(!synth_output_active)return;
    if((uint32_t)(engine_write-engine_read)>=384)return;
    int16_t block[192];
    engine_wave(synth_wave);
    engine_render(block);
    uint32_t write=engine_write;
    for(unsigned i=0;i<192;++i)engine_ring[(write+i)%768]=block[i];
    __DMB();
    engine_write=write+192;
}

uint32_t tusb_time_millis_api(void){return ticks;}
void SysTick_Handler(void){++ticks;}
void OTG_FS_IRQHandler(void){tusb_int_handler(0,true);}

bool synth_clock_init(void)
{
    RCC->APB1ENR|=RCC_APB1ENR_PWREN;
    PWR->CR|=PWR_CR_VOS;
    RCC->CR|=RCC_CR_HSEON;
    unsigned timeout=1000000;
    while(!(RCC->CR&RCC_CR_HSERDY) && --timeout){}
    if(!timeout)return false;
    FLASH->ACR=FLASH_ACR_ICEN|FLASH_ACR_DCEN|FLASH_ACR_PRFTEN|FLASH_ACR_LATENCY_5WS;
    RCC->PLLCFGR=8u|(336u<<6)|RCC_PLLCFGR_PLLSRC_HSE|(7u<<24);
    RCC->CR|=RCC_CR_PLLON;
    timeout=1000000;
    while(!(RCC->CR&RCC_CR_PLLRDY) && --timeout){}
    if(!timeout)return false;
    RCC->CFGR=RCC_CFGR_PPRE1_DIV4|RCC_CFGR_PPRE2_DIV2|RCC_CFGR_SW_PLL;
    while((RCC->CFGR&RCC_CFGR_SWS)!=RCC_CFGR_SWS_PLL){}
    SystemCoreClock=168000000;
    SysTick_Config(SystemCoreClock/1000);
    NVIC_SetPriority(SysTick_IRQn,2);
    return true;
}

static void generate(int16_t *output)
{
    for(unsigned i=0;i<48;++i){
        int32_t value=0;
        if(synth_playing){
            unsigned count=synth_sample_count;
            if(sample_position<count)value=sample_memory[sample_position++];
            else synth_playing=false;
        }else{
            if(engine_read!=engine_write){
                value=engine_ring[engine_read%768];
                ++engine_read;
            }else ++engine_underruns;
            if(synth_tone){tone_phase+=39370534u;value+=sine_table[tone_phase>>24]/4;}
        }
        value=value*(int32_t)synth_volume/100;
        if(value>32767)value=32767;
        if(value< -32768)value=-32768;
        output[i]=value;
    }
}

void synth_note_on(unsigned note,unsigned velocity)
{
    engine_note_on(note,velocity);
}
void synth_note_off(unsigned note)
{
    engine_note_off(note);
}

void synth_record(void)
{
    uint32_t mask=__get_PRIMASK();__disable_irq();
    synth_playing=false;synth_sample_count=0;sample_position=0;synth_recording=true;
    __set_PRIMASK(mask);
}

void synth_play(void)
{
    uint32_t mask=__get_PRIMASK();__disable_irq();
    synth_recording=false;sample_position=0;synth_playing=synth_sample_count>0;
    __set_PRIMASK(mask);
}

bool tud_audio_set_itf_cb(uint8_t port,const tusb_control_request_t *request)
{
    (void)port;
    unsigned itf=request->wIndex&255,alt=request->wValue&255;
    if(itf==1)synth_input_active=alt!=0;
    if(itf==2){
        synth_output_active=alt!=0;
        if(alt){
            engine_read=engine_write;
            int16_t packet[48];
            generate(packet);tud_audio_write(packet,sizeof(packet));
            generate(packet);tud_audio_write(packet,sizeof(packet));
        }
    }
    return true;
}

bool tud_audio_set_itf_close_ep_cb(uint8_t port,const tusb_control_request_t *request)
{
    (void)port;
    if((request->wIndex&255)==1)synth_input_active=false;
    if((request->wIndex&255)==2)synth_output_active=false;
    return true;
}

bool tud_audio_tx_done_isr(uint8_t port,uint16_t bytes,uint8_t function,uint8_t endpoint,uint8_t alternate)
{
    (void)port;(void)bytes;(void)function;(void)endpoint;(void)alternate;
    int16_t packet[48];
    generate(packet);tud_audio_write(packet,sizeof(packet));
    ++synth_frames_in;
    return true;
}

bool tud_audio_rx_done_isr(uint8_t port,uint16_t bytes,uint8_t function,uint8_t endpoint,uint8_t alternate)
{
    (void)port;(void)bytes;(void)function;(void)endpoint;(void)alternate;
    int16_t packet[48];
    unsigned count=tud_audio_read(packet,sizeof(packet))/2;
    if(synth_recording){
        unsigned position=synth_sample_count;
        for(unsigned i=0;i<count && position<24000;++i)sample_memory[position++]=packet[i];
        synth_sample_count=position;
        if(position==24000)synth_recording=false;
    }
    ++synth_frames_out;
    return true;
}

bool tud_audio_set_req_ep_cb(uint8_t port,const tusb_control_request_t *request,uint8_t *data)
{
    (void)port;
    return request->bRequest==1 && request->wLength==3 &&
        ((request->wValue>>8)==1) && data[0]==0x80 && data[1]==0xbb && data[2]==0;
}

bool tud_audio_get_req_ep_cb(uint8_t port,const tusb_control_request_t *request)
{
    static uint8_t rate[]={0x80,0xbb,0};
    if(request->bRequest==0x81 && (request->wValue>>8)==1)
        return tud_audio_buffer_and_schedule_control_xfer(port,request,rate,sizeof(rate));
    return false;
}

void tud_umount_cb(void)
{
    synth_input_active=false;synth_output_active=false;synth_recording=false;
    for(unsigned i=0;i<128;++i)synth_note_off(i);
}

static const unsigned fx_parameters[]={LPF_CUTOFF,LPF_Q,EG_ATTACK_TIM,EG_RELEASE_TIME,VIB_AMP,TRE_AMP};
static unsigned fx_values[]={30000,71,15,250,0,0};
static const unsigned fx_steps[]={1536,10,20,50,32,32};
static const unsigned fx_min[]={0,10,1,1,0,0};
static const unsigned fx_max[]={34579,400,2000,4000,512,512};
unsigned synth_fx_value(unsigned index){return index<6?fx_values[index]:0;}
void synth_fx_adjust(unsigned index,int direction)
{
    if(index>=6)return;
    int value=(int)fx_values[index]+direction*(int)fx_steps[index];
    if(value<(int)fx_min[index])value=fx_min[index];
    if(value>(int)fx_max[index])value=fx_max[index];
    fx_values[index]=value;
    engine_parameter(fx_parameters[index],value);
    engine_parameter(VIB_LFO,18000);
    engine_parameter(TRE_LFO,15000);
}

void synth_poll(void)
{
    uint8_t packet[4];
    while(tud_midi_packet_read(packet)){
        unsigned status=packet[1]&0xf0;
        if(status==0x90)synth_note_on(packet[2]&127,packet[3]&127);
        else if(status==0x80)synth_note_off(packet[2]&127);
        else if(status==0xb0 && packet[2]==74){
            fx_values[0]=(packet[3]&127)*34579/127;engine_parameter(LPF_CUTOFF,fx_values[0]);
        }else if(status==0xb0 && packet[2]==71){
            fx_values[1]=10+(packet[3]&127)*390/127;engine_parameter(LPF_Q,fx_values[1]);
        }else if(status==0xb0 && packet[2]==1){
            fx_values[4]=(packet[3]&127)*512/127;engine_parameter(VIB_AMP,fx_values[4]);engine_parameter(VIB_LFO,18000);
        }
        else if(status==0xb0 && (packet[2]==120||packet[2]==123))
            for(unsigned i=0;i<128;++i)synth_note_off(i);
    }
}

void synth_usb_init(void)
{
    for(unsigned i=0;i<256;++i)sine_table[i]=24576*sinf(i*6.28318530718f/256);
    engine_init();
    RCC->AHB1ENR|=RCC_AHB1ENR_GPIOAEN;
    GPIOA->MODER=(GPIOA->MODER&~((3u<<22)|(3u<<24)))|(2u<<22)|(2u<<24);
    GPIOA->AFR[1]=(GPIOA->AFR[1]&~((15u<<12)|(15u<<16)))|(10u<<12)|(10u<<16);
    GPIOA->OSPEEDR|=(3u<<22)|(3u<<24);
    RCC->AHB2ENR|=RCC_AHB2ENR_OTGFSEN;
    NVIC_SetPriority(OTG_FS_IRQn,1);
    tusb_rhport_init_t config={.role=TUSB_ROLE_DEVICE,.speed=TUSB_SPEED_FULL};
    synth_usb_initialized=tusb_init(0,&config);
}
