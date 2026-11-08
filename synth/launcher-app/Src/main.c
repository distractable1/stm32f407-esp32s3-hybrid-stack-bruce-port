#include "board_support.h"
#include "synth_usb.h"
#include "tusb.h"
#include <stdio.h>
static unsigned usb_running;
static void service_usb(void){if(usb_running){tud_task_ext(0,false);synth_audio_task();}}
void board_background(void){service_usb();}
static void draw_keyboard(int held)
{
    const char *labels[]={"C","D","E","F","G","A","B","C"};
    for(unsigned i=0;i<8;++i){
        rectangle(i*40,147,39,86,held==(int)i?CYAN:WHITE);
        text(i*40+15,211,labels[i],BLACK);
    }
}

static void draw_synth(void)
{
    rectangle(0,0,320,240,BLACK);
    text(8,6,"STM32 USB SYNTH",WHITE);
    panel_button(224,0,88,"FX",0);
    text(8,25,"48 kHz / 16-bit / mono",CYAN);
    panel_button(8,46,96,"TONE 440",synth_tone);
    const char *waves[]={"SINE","SQUARE","SAW","TRIANGLE"};
    panel_button(112,46,104,waves[synth_wave],0);
    panel_button(224,46,40,"-",0);
    panel_button(272,46,40,"+",0);
    panel_button(8,81,148,"RECORD 0.5s",synth_recording);
    panel_button(164,81,148,"PLAY SAMPLE",synth_playing);
    draw_keyboard(-1);
}

static void status_line(const char *value){board_status_line(8,119,value,WHITE,BLACK);}
static unsigned effects_page;
static void draw_effects(void)
{
    const char *names[]={"CUTOFF","RESONANCE","ATTACK ms","RELEASE ms","VIBRATO","TREMOLO"};
    rectangle(0,0,320,240,BLACK);
    text(8,6,"SYNTH EFFECTS",CYAN);
    panel_button(224,0,88,"KEYS",0);
    for(unsigned i=0;i<6;++i){
        char line[28];
        snprintf(line,sizeof(line),"%s %u",names[i],synth_fx_value(i));
        text(8,42+i*27,line,WHITE);
        panel_button(232,35+i*27,32,"-",0);
        panel_button(272,35+i*27,40,"+",0);
    }
    text(8,216,"Hold WK_UP: launcher",WHITE);
}
static void redraw_interface(void)
{
    status_line(0);
    if(effects_page)draw_effects();
    else draw_synth();
}
int main(void){
    if(!board_init())for(;;){}
    synth_usb_init();usb_running=synth_usb_initialized;
    redraw_interface();
    int held=-1;
    unsigned button_held=0;
    uint32_t last_status=0;
    const unsigned notes[]={60,62,64,65,67,69,71,72};
    for(;;){
        service_usb();
        synth_poll();
        unsigned buttons=physical_buttons();
        if(buttons){
            if(held>=0){synth_note_off(notes[held]);held=-1;}
            if(buttons&1){
                for(unsigned i=0;i<128;++i)synth_note_off(i);
                board_recalibrate();
                redraw_interface();
            }
            if(buttons&8){tud_disconnect();delay_ms(20);board_return_to_launcher();}
            if(buttons&2)synth_play();
            if(buttons&4){effects_page=!effects_page;redraw_interface();}
        }
        int key=-1;
        int x,y;
        if(board_touch(&x,&y)){
            {
                if(y<30 && x>=224){
                    if(!button_held){button_held=1;effects_page=!effects_page;redraw_interface();}
                }else if(effects_page){
                    if(!button_held && x>=232 && y>=35 && y<197){
                        unsigned row=(y-35)/27;
                        if(row<6){button_held=1;synth_fx_adjust(row,x<268?-1:1);draw_effects();}
                    }
                }else if(y>=147)key=x/40;
                else if(!button_held){
                    button_held=1;
                    if(y<25){effects_page=1;redraw_interface();}
                    else if(y>=46 && y<74){
                        if(x<108){synth_tone=!synth_tone;panel_button(8,46,96,"TONE 440",synth_tone);}
                        else if(x<220){
                            synth_wave=(synth_wave+1)%4;
                            const char *waves[]={"SINE","SQUARE","SAW","TRIANGLE"};
                            panel_button(112,46,104,waves[synth_wave],0);
                        }else if(x<268){if(synth_volume>=5)synth_volume-=5;}
                        else if(synth_volume<=95)synth_volume+=5;
                    }else if(y>=81 && y<109){
                        if(x<160)synth_record();else synth_play();
                    }
                }
            }
        }else button_held=0;
        if(!effects_page && key!=held){
            if(held>=0)synth_note_off(notes[held]);
            if(key>=0)synth_note_on(notes[key],110);
            held=key;draw_keyboard(held);
        }else if(key<0 && held>=0){synth_note_off(notes[held]);held=-1;}
        uint32_t now=tusb_time_millis_api();
        if(now-last_status>=250 && !effects_page){
            char line[40];
            if(!tud_mounted())snprintf(line,sizeof(line),"USB waiting for phone / PC");
            else if(synth_recording)snprintf(line,sizeof(line),"Recording %lu/24000",(unsigned long)synth_sample_count);
            else if(synth_playing)snprintf(line,sizeof(line),"Playing sample | VOL %u",synth_volume);
            else snprintf(line,sizeof(line),"USB TX:%s RX:%s VOL:%u N:%lu",synth_output_active?"ON":"--",synth_input_active?"ON":"--",synth_volume,(unsigned long)synth_sample_count);
            status_line(line);last_status=now;
        }
        delay_ms(3);
    }
}

