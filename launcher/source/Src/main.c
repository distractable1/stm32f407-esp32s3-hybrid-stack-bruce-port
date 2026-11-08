#include "board_support.h"
#include "launcher.h"
#include <stdio.h>
#include <string.h>

static LauncherEntry entries[5],installed;
static unsigned page,count,selected;
static bool more,have_installed;
static char message[39];

static void set_message(const char *value){snprintf(message,sizeof(message),"%.38s",value);}
static void scan_apps(void){
    if(!launcher_list(page,entries,&count,&more))set_message(launcher_error());
    else set_message(count?"Select an app, then INSTALL":"Copy .APP files into SD:/APPS");
    if(selected>=count)selected=0;
    have_installed=launcher_installed(&installed);
}
static void draw_menu(void){
    board_status_line(0,0,0,0,0);
    rectangle(0,0,320,240,BLACK);
    text(8,6,"SD APP LAUNCHER",CYAN);
    if(have_installed){
        char label[39];snprintf(label,sizeof(label),"RUN %.31s",installed.name);
        panel_button(8,32,304,label,0);
    }else text(8,39,"No valid installed app",WHITE);
    for(unsigned i=0;i<count;++i){
        rectangle(8,69+i*25,304,24,i==selected?0x2d7f:0x18e4);
        text(13,74+i*25,entries[i].name,WHITE);
    }
    panel_button(8,196,48,"<",0);
    panel_button(64,196,48,">",0);
    panel_button(120,196,80,"SCAN",0);
    panel_button(208,196,104,"INSTALL",0);
    board_status_line(8,226,message,WHITE,BLACK);
}
static void progress(unsigned percent,const char *phase){
    static char previous[40];
    if(strcmp(previous,phase)){
        snprintf(previous,sizeof(previous),"%s",phase);
        rectangle(0,0,320,240,BLACK);
        text(8,32,"Installing app",CYAN);
        text(8,65,phase,WHITE);
        text(8,175,"Keep power connected",WHITE);
    }
    rectangle(8,105,304,22,0x18e4);
    rectangle(8,105,304*percent/100,22,CYAN);
    char value[12];snprintf(value,sizeof(value),"%u%%",percent);
    rectangle(8,140,100,16,BLACK);text(8,140,value,WHITE);
}
static void run_installed(void){
    int x,y;while(board_touch(&x,&y))delay_ms(10);
    if(!launcher_request_boot()){set_message(launcher_error());draw_menu();}
}
static void install_selected(void){
    if(!count){set_message("No compatible app selected");draw_menu();return;}
    if(!launcher_install(entries[selected].filename,progress)){
        set_message(launcher_error());have_installed=launcher_installed(&installed);draw_menu();return;
    }
    run_installed();
}
int main(void){
    if(!board_init())for(;;){}
    scan_apps();draw_menu();
    unsigned held=0;
    for(;;){
        unsigned buttons=physical_buttons();
        if(buttons&1){board_recalibrate();scan_apps();draw_menu();}
        if(buttons&4 && count){selected=(selected+1)%count;draw_menu();}
        if(buttons&2)install_selected();
        int x,y;
        if(board_touch(&x,&y)){
            if(!held){
                held=1;
                if(y>=32 && y<60 && have_installed)run_installed();
                else if(y>=69 && y<194){
                    unsigned row=(y-69)/25;if(row<count){selected=row;draw_menu();}
                }else if(y>=196 && y<224){
                    if(x<56){if(page){--page;scan_apps();draw_menu();}}
                    else if(x<112){if(more){++page;scan_apps();draw_menu();}}
                    else if(x<200){page=0;scan_apps();draw_menu();}
                    else install_selected();
                }
            }
        }else held=0;
        delay_ms(3);
    }
}
