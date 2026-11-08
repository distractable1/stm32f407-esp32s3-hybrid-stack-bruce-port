#include "engine_usb.h"
#include "elementary_wave.h"
#include <string.h>

extern uint32_t _status;
extern float32_t _audio_out[SYNTH_SAMPLE_ALL];
static uint16_t controls[SIZE_PARAMATER];
static uint16_t unused_dac[SYNTH_NUM_SAMPLING];
static unsigned assigned_note[10], next_voice;

void engine_init(void)
{
    memset(controls,0,sizeof(controls));
    for(unsigned i=0;i<10;++i)assigned_note[i]=128;
    controls[M_VOLUME]=80;controls[SYNC_WAVFORM]=1;
    controls[S1_SIN]=512;
    controls[EG_ATTACK_TIM]=15;controls[EG_ATTACK_LEVEL]=150;
    controls[EG_DECAY_TIME]=150;controls[EG_RELEASE_TIME]=250;
    controls[LPF_CUTOFF]=30000;controls[LPF_Q]=71;
    controls[PITCH]=256;
    set_synth_para(unused_dac,controls);
}
void engine_note_on(unsigned note,unsigned velocity)
{
    if(note>127)return;
    if(!velocity){engine_note_off(note);return;}
    unsigned index=10;
    for(unsigned i=0;i<10;++i)if(assigned_note[i]==note){index=i;break;}
    if(index==10)
        for(unsigned i=0;i<10;++i)if(assigned_note[i]==128){index=i;break;}
    if(index==10)index=next_voice++%10;
    assigned_note[index]=note;
    controls[S1_FREQ+6*index]=note*256;
    controls[S1_ENABLE+6*index]=(velocity*100+126)/127;
}
void engine_note_off(unsigned note)
{
    for(unsigned i=0;i<10;++i)if(assigned_note[i]==note){
        controls[S1_ENABLE+6*i]=0;assigned_note[i]=128;
    }
}
void engine_wave(unsigned wave)
{
    controls[S1_SIN]=wave==0?512:0;
    controls[S1_SQU]=wave==1?512:0;
    controls[S1_SAW]=wave==2?512:0;
    controls[S1_TRI]=wave==3?512:0;
}
void engine_parameter(unsigned parameter,unsigned value)
{
    if(parameter<SIZE_PARAMATER)controls[parameter]=value>65535?65535:value;
}
unsigned engine_block_size(void){return SYNTH_NUM_SAMPLING;}
void engine_render(int16_t *output)
{
    _status=SYNTH_STATUS_LFOC;
    synth_process();
    synth_process();
    for(unsigned i=0;i<SYNTH_NUM_SAMPLING;++i){
        float sample=(_audio_out[SYNTH_PRE_SAMPLE+i]-SYNTH_GND_LEVEL)*16.0f;
        if(!(sample>=-32768 && sample<=32767)){
            if(sample>32767)sample=32767;
            else if(sample< -32768)sample=-32768;
            else sample=0;
        }
        output[i]=(int16_t)sample;
    }
}
