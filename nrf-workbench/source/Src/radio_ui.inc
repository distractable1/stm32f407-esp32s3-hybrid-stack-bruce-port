/* Included after the shared display and calibration routines. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "FatFs/ff.h"
#include "nrf_radio.h"

#define PANEL 0x10c3
#define MUTED 0x8c71
#define ACCENT 0x2d7f
typedef struct { char name[13]; RadioProfile radio; } NamedProfile;
static NamedProfile profiles[8] = {{"LAB",{{0xe7,0xe7,0xe7,0xe7,0xe7},5,40,1,32,1,2}}};
static unsigned profile_count=1, selected_profile;
static FATFS filesystem;
static FIL logfile;
static unsigned sd_ready, radio_ready, logging, page, paused, selected_channel=40;
static unsigned channel_hits[84], channel_samples[84], next_channel;
static unsigned received, transmitted, bad_packets;
static uint8_t last_packet[32];
static unsigned last_length, have_packet;
static char last_hex[65], filename[32], notice[40]="Ready";
static uint32_t last_sync;

static void run_calibration(void);

static uint32_t milliseconds(void)
{
    static uint32_t previous, total;
    uint32_t now=REG(0xe0001004u);
    uint32_t elapsed=(uint32_t)(now-previous)/16000u;
    previous+=elapsed*16000u;
    total+=elapsed;
    return total;
}

static unsigned log_close(void)
{
    if(!logging)return 1;
    logging=0;
    FRESULT result=f_close(&logfile);
    if(result!=FR_OK){sd_ready=0;strcpy(notice,"SD close error");return 0;}
    strcpy(notice,"Log closed safely");
    return 1;
}

static unsigned log_bytes(const char *data,unsigned length)
{
    if(!logging)return 0;
    UINT written=0;
    FRESULT result=f_write(&logfile,data,length,&written);
    if(result!=FR_OK || written!=length){
        log_close();sd_ready=0;strcpy(notice,"SD write failed / card full");
        return 0;
    }
    return 1;
}

static void log_start(unsigned packets)
{
    if(logging){log_close();return;}
    if(!sd_ready){strcpy(notice,"Mount a FAT32 SD card first");return;}
    for(unsigned index=0;index<1000;++index){
        snprintf(filename,sizeof(filename),"0:/NRFLOG/%c%03u.CSV",packets?'P':'S',index);
        FRESULT result=f_open(&logfile,filename,FA_WRITE|FA_CREATE_NEW);
        if(result==FR_EXIST)continue;
        if(result!=FR_OK){strcpy(notice,"Cannot create SD log");return;}
        logging=1;
        const char *header=packets?
            "uptime_ms,profile,channel,mhz,kbps,address_lsb_first,crc_bytes,dynamic,length,payload_hex\r\n":
            "uptime_ms,channel,mhz,hits,samples\r\n";
        log_bytes(header,strlen(header));
        last_sync=milliseconds();
        strcpy(notice,"Recording; stop before unplug");
        return;
    }
    strcpy(notice,"Log names exhausted");
}

static unsigned load_calibration(void)
{
    FIL file;UINT count=0;
    struct { uint32_t magic; Point points[5]; uint32_t checksum; } record;
    if(!sd_ready || f_open(&file,"0:/NRFLOG/TOUCH.BIN",FA_READ)!=FR_OK)return 0;
    FRESULT result=f_read(&file,&record,sizeof(record),&count);
    f_close(&file);
    if(result!=FR_OK || count!=sizeof(record) || record.magic!=0x324c4143u)return 0;
    uint32_t checksum=0;
    const uint8_t *bytes=(const uint8_t *)&record;
    for(unsigned i=0;i<sizeof(record)-4;++i)checksum=checksum*33+bytes[i];
    if(checksum!=record.checksum)return 0;
    memcpy(raw,record.points,sizeof(raw));
    for(unsigned i=0;i<5;++i)
        if(!(raw[i].x>=0 && raw[i].x<=4095 && raw[i].y>=0 && raw[i].y<=4095))return 0;
    return calibrate();
}

static void save_calibration(void)
{
    if(!sd_ready)return;
    struct { uint32_t magic; Point points[5]; uint32_t checksum; } record;
    record.magic=0x324c4143u;
    memcpy(record.points,raw,sizeof(raw));
    record.checksum=0;
    const uint8_t *bytes=(const uint8_t *)&record;
    for(unsigned i=0;i<sizeof(record)-4;++i)record.checksum=record.checksum*33+bytes[i];
    FIL file;UINT written=0;
    if(f_open(&file,"0:/NRFLOG/TOUCH.BIN",FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK){strcpy(notice,"Calibration save failed");return;}
    FRESULT result=f_write(&file,&record,sizeof(record),&written);
    FRESULT closed=f_close(&file);
    if(result!=FR_OK || written!=sizeof(record) || closed!=FR_OK)strcpy(notice,"Calibration save failed");
}

static unsigned hex_address(const char *text_value,uint8_t address[5],uint8_t *length)
{
    unsigned count=strlen(text_value);
    if(count<6 || count>10 || count%2)return 0;
    for(unsigned i=0;i<count;++i){
        char c=text_value[i];
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F')))return 0;
    }
    *length=count/2;
    for(unsigned i=0;i<*length;++i){
        char pair[3]={text_value[2*i],text_value[2*i+1],0};
        address[i]=strtoul(pair,0,16);
    }
    return 1;
}

static void load_profiles(void)
{
    if(!sd_ready)return;
    FIL file;
    if(f_open(&file,"0:/NRFLOG/PROFILES.CSV",FA_READ)!=FR_OK){
        const char template[]="name,channel,kbps,address_lsb_first,length,dynamic,crc_bytes\r\nLAB,40,1000,E7E7E7E7E7,32,1,2\r\n";
        if(f_open(&file,"0:/NRFLOG/PROFILES.CSV",FA_WRITE|FA_CREATE_NEW)==FR_OK){
            UINT written;f_write(&file,template,sizeof(template)-1,&written);f_close(&file);
        }
        return;
    }
    char content[2048];UINT count=0;
    FRESULT result=f_read(&file,content,sizeof(content)-1,&count);
    FSIZE_t size=f_size(&file);f_close(&file);
    if(result!=FR_OK || size>=sizeof(content)){strcpy(notice,"Profiles file too large/error");return;}
    content[count]=0;
    unsigned loaded=0;
    char *line=strtok(content,"\r\n");
    while(line && loaded<6){
        char name[13],address[11];
        unsigned channel,rate,length,dynamic,crc;
        if(sscanf(line,"%12[^,],%u,%u,%10[^,],%u,%u,%u",name,&channel,&rate,address,&length,&dynamic,&crc)==7){
            NamedProfile candidate={0};
            if(channel<=83 && (rate==250||rate==1000||rate==2000) && length>=1 && length<=32 &&
               dynamic<=1 && crc<=2 && hex_address(address,candidate.radio.address,&candidate.radio.address_length)){
                strcpy(candidate.name,name);
                candidate.radio.channel=channel;
                candidate.radio.rate=rate==250?0:rate==1000?1:2;
                candidate.radio.payload_length=length;
                candidate.radio.dynamic_payload=dynamic;
                candidate.radio.crc_bytes=crc;
                profiles[loaded++]=candidate;
            }
        }
        line=strtok(0,"\r\n");
    }
    if(loaded){profile_count=loaded;selected_profile=0;strcpy(notice,"Profiles loaded");}
}

static void mount_sd(void)
{
    log_close();
    sd_ready=f_mount(&filesystem,"0:",1)==FR_OK;
    if(sd_ready){
        FRESULT result=f_mkdir("0:/NRFLOG");
        if(result!=FR_OK && result!=FR_EXIST)sd_ready=0;
    }
    strcpy(notice,sd_ready?"SD mounted":"SD absent / mount failed");
    if(sd_ready)load_profiles();
}

static unsigned kbps(const RadioProfile *p) {return p->rate==0?250:p->rate==1?1000:2000;}

static void button(int x,int y,int width,const char *label,unsigned active)
{
    rectangle(x,y,width,27,active?ACCENT:PANEL);
    text(x+7,y+6,label,WHITE);
}

static void address_text(char *out,const RadioProfile *p)
{
    for(unsigned i=0;i<p->address_length;++i)sprintf(out+i*2,"%02X",p->address[i]);
}


static char rendered_fields[8][40];
static unsigned rendered_colors[8];
static int rendered_heights[84];
static unsigned rendered_selection=84;
static unsigned rendered_packet_button=2;

static void invalidate_display(void)
{
    memset(rendered_fields,0,sizeof(rendered_fields));
    for(unsigned i=0;i<84;++i)rendered_heights[i]=-1;
    rendered_selection=84;
    rendered_packet_button=2;
}

/* Opaque character tiles erase only characters that actually changed. */
static void update_field(unsigned slot,int x,int y,unsigned width,const char *value,unsigned color)
{
    unsigned length=strlen(value);
    for(unsigned i=0;i<width;++i){
        unsigned ch=i<length?(unsigned char)value[i]:' ';
        if(ch<32 || ch>126)ch='?';
        if(rendered_fields[slot][i]==(char)ch && rendered_colors[slot]==color)continue;
        unsigned left=x+i*8, right=left+7, bottom=y+13;
        COMMAND(0x2a,left>>8,left,right>>8,right);
        COMMAND(0x2b,y>>8,y,bottom>>8,bottom);
        lcd_write(0x2c,0);
        for(unsigned row=0;row<14;++row)
            for(unsigned col=0;col<8;++col)
                lcd_write((font[ch-32][row]&(1u<<col))?color:BLACK,1);
        rendered_fields[slot][i]=ch;
    }
    rendered_colors[slot]=color;
}

static void refresh_values(void)
{
    char line[64];
    update_field(0,8,23,19,radio_ready?"RADIO OK":"RADIO MISSING",radio_ready?GREEN:RED);
    update_field(1,176,23,17,sd_ready?"SD READY":"SD OFFLINE",sd_ready?GREEN:MUTED);
    if(page==0){
        for(unsigned i=0;i<84;++i){
            unsigned height=channel_samples[i]?channel_hits[i]*80/channel_samples[i]:0;
            int old=rendered_heights[i];
            unsigned color=i==selected_channel?WHITE:CYAN;
            if(old<0 || (rendered_selection!=selected_channel &&
                        (i==rendered_selection || i==selected_channel))){
                rectangle(27+i*3,91,2,81,BLACK);
                rectangle(27+i*3,171-height,2,height+1,color);
            }else if((unsigned)old<height){
                rectangle(27+i*3,171-height,2,height-old,color);
            }else if((unsigned)old>height){
                rectangle(27+i*3,171-old,2,old-height,BLACK);
            }
            rendered_heights[i]=height;
        }
        rendered_selection=selected_channel;
        snprintf(line,sizeof(line),"CH %02u  %u MHz  hits %u/%u",selected_channel,2400+selected_channel,channel_hits[selected_channel],channel_samples[selected_channel]);
        update_field(2,8,193,38,line,WHITE);
    }else if(page==1){
        snprintf(line,sizeof(line),"RX %u  TX %u  bad %u",received,transmitted,bad_packets);
        update_field(3,8,112,38,line,GREEN);
        update_field(4,8,132,38,have_packet?"LATEST PAYLOAD":"WAITING FOR MATCHING PACKETS",MUTED);
        char half[33]={0};
        if(have_packet)memcpy(half,last_hex,32);
        update_field(5,8,148,32,half,CYAN);
        update_field(6,8,163,32,have_packet?last_hex+32:"",CYAN);
        if(rendered_packet_button!=have_packet){
            button(128,181,184,have_packet?"SEND LAST ONCE":"NO PACKET TO SEND",0);
            rendered_packet_button=have_packet;
        }
    }else if(page==3){
        update_field(7,8,194,38,notice,MUTED);
    }
}

static void refresh_controls(void)
{
    if(page==0){
        button(8,43,96,paused?"RESUME":"PAUSE",!paused);
        button(112,43,96,logging?"STOP LOG":"LOG SCAN",logging);
    }else if(page==1){
        NamedProfile *p=&profiles[selected_profile];
        button(8,43,136,p->name,1);
        button(8,181,112,logging?"STOP LOG":"LOG RX",logging);
        char line[64],addr[11];
        snprintf(line,sizeof(line),"CH %02u / %u MHz / %u kbps",p->radio.channel,2400+p->radio.channel,kbps(&p->radio));
        rectangle(8,77,304,14,BLACK);text(8,77,line,WHITE);
        address_text(addr,&p->radio);
        snprintf(line,sizeof(line),"ADDR %s  CRC %u",addr,p->radio.crc_bytes);
        rectangle(8,94,304,14,BLACK);text(8,94,line,MUTED);
    }else if(page==2){
        for(unsigned i=0;i<profile_count && i<6;++i){
            char line[64];
            snprintf(line,sizeof(line),"%s  CH%02u %uk",profiles[i].name,profiles[i].radio.channel,kbps(&profiles[i].radio));
            rectangle(8,64+i*21,304,20,i==selected_profile?ACCENT:PANEL);
            text(14,67+i*21,line,WHITE);
        }
    }
    refresh_values();
}

static void draw_page(void)
{
    invalidate_display();
    char line[64];
    rectangle(0,0,320,240,BLACK);
    text(8,5,"NRF24 WORKBENCH",WHITE);
    text(8,23,radio_ready?"RADIO OK":"RADIO MISSING",radio_ready?GREEN:RED);
    text(176,23,sd_ready?"SD READY":"SD OFFLINE",sd_ready?GREEN:MUTED);
    const char *tabs[]={"SCAN","RECEIVE","PROFILES","SD / CAL"};
    for(unsigned i=0;i<4;++i)button(i*80,212,78,tabs[i],page==i);
    if(page==0){
        button(8,43,96,paused?"RESUME":"PAUSE",!paused);
        button(112,43,96,logging?"STOP LOG":"LOG SCAN",logging);
        button(216,43,96,"CLEAR",0);
        text(8,74,"CHANNEL ACTIVITY  (RPD)",MUTED);
        for(unsigned i=0;i<84;++i){
            unsigned height=channel_samples[i]?channel_hits[i]*80/channel_samples[i]:0;
            rectangle(27+i*3,171-height,2,height+1,i==selected_channel?WHITE:CYAN);
        }
        text(24,174,"0          40          83",MUTED);
        snprintf(line,sizeof(line),"CH %02u  %u MHz  hits %u/%u",selected_channel,2400+selected_channel,channel_hits[selected_channel],channel_samples[selected_channel]);
        text(8,193,line,WHITE);
    }else if(page==1){
        NamedProfile *p=&profiles[selected_profile];
        button(8,43,136,p->name,1);
        button(152,43,76,"CH -",0);button(236,43,76,"CH +",0);
        snprintf(line,sizeof(line),"CH %02u / %u MHz / %u kbps",p->radio.channel,2400+p->radio.channel,kbps(&p->radio));
        text(8,77,line,WHITE);
        char addr[11];address_text(addr,&p->radio);
        snprintf(line,sizeof(line),"ADDR %s  CRC %u",addr,p->radio.crc_bytes);text(8,94,line,MUTED);
        snprintf(line,sizeof(line),"RX %u  TX %u  bad %u",received,transmitted,bad_packets);text(8,112,line,GREEN);
        text(8,132,have_packet?"LATEST PAYLOAD":"WAITING FOR MATCHING PACKETS",MUTED);
        if(have_packet){char half[33];memcpy(half,last_hex,32);half[32]=0;text(8,148,half,CYAN);text(8,163,last_hex+32,CYAN);}
        button(8,181,112,logging?"STOP LOG":"LOG RX",logging);
        button(128,181,184,have_packet?"SEND LAST ONCE":"NO PACKET TO SEND",0);
    }else if(page==2){
        text(8,43,"SELECT RECEIVE PROFILE",MUTED);
        for(unsigned i=0;i<profile_count;++i){
            if(i>=6)break;
            snprintf(line,sizeof(line),"%s  CH%02u %uk",profiles[i].name,profiles[i].radio.channel,kbps(&profiles[i].radio));
            rectangle(8,64+i*21,304,20,i==selected_profile?ACCENT:PANEL);
            text(14,67+i*21,line,WHITE);
        }
        text(8,194,"Edit NRFLOG/PROFILES.CSV on PC",MUTED);
    }else{
        text(8,44,"SD LOGS + TOUCH SETTINGS",WHITE);
        button(8,66,148,"MOUNT / RELOAD",0);button(164,66,148,"RECALIBRATE",0);
        text(8,103,"Directory: /NRFLOG",MUTED);
        text(8,121,"Pnnn.CSV = packet payloads",WHITE);
        text(8,139,"Snnn.CSV = channel activity",WHITE);
        text(8,157,"Stop logging before removal",WHITE);
        if(filename[0])text(8,176,filename+3,CYAN);
        text(8,194,notice,MUTED);
    }
}

static unsigned ui_touch(int *x,int *y)
{
    static unsigned held;
    if(!touching()){held=0;return 0;}
    if(held)return 0;
    Point p=read_touch();
    if(!touching())return 0;
    float px=transform[0][0]*p.x/4095+transform[0][1]*p.y/4095+transform[0][2];
    float py=transform[1][0]*p.x/4095+transform[1][1]*p.y/4095+transform[1][2];
    if(!(px>=0 && px<320 && py>=0 && py<240))return 0;
    *x=px;*y=py;held=1;return 1;
}

static void select_rx(void)
{
    have_packet=0;last_length=0;last_hex[0]=0;
    if(radio_ready && !radio_receive(&profiles[selected_profile].radio))strcpy(notice,"Radio profile rejected");
}

static void packet_received(void)
{
    unsigned length=0;
    int result=radio_read(last_packet,&length);
    if(result<0){++bad_packets;return;}
    if(result==0)return;
    ++received;last_length=length;have_packet=1;
    memset(last_hex,0,sizeof(last_hex));
    for(unsigned i=0;i<length;++i)sprintf(last_hex+2*i,"%02X",last_packet[i]);
    if(logging){
        char row[192],addr[11];
        NamedProfile *p=&profiles[selected_profile];address_text(addr,&p->radio);
        int count=snprintf(row,sizeof(row),"%lu,%s,%u,%u,%u,%s,%u,%u,%u,%s\r\n",
            (unsigned long)milliseconds(),p->name,p->radio.channel,2400+p->radio.channel,kbps(&p->radio),
            addr,p->radio.crc_bytes,p->radio.dynamic_payload,length,last_hex);
        if(count>0 && count<(int)sizeof(row))log_bytes(row,count);
    }
}

static void radio_application(void)
{
    REG(0xe000edfc)|=1u<<24;REG(0xe0001000)|=1;
    radio_ready=radio_init();
    serial(radio_ready?"Radio SPI OK\r\n":"Radio not detected\r\n");
    draw_page();
    uint32_t last_redraw=milliseconds();
    for(;;){
        int x,y;
        if(ui_touch(&x,&y)){
            unsigned previous_page=page;
            unsigned recalibrated=0;
            if(y>=212){
                unsigned next=x/80;
                if(next!=page){
                    log_close();
                    if(radio_ready)radio_stop();
                    page=next;
                    if(page==1)select_rx();
                }
            }else if(page==0){
                if(y>=43 && y<=70){
                    if(x<108)paused=!paused;
                    else if(x<212)log_start(0);
                    else{memset(channel_hits,0,sizeof(channel_hits));memset(channel_samples,0,sizeof(channel_samples));}
                }else if(y>=82 && y<192 && x>=27 && x<279){
                    selected_channel=(x-27)/3;
                    profiles[selected_profile].radio.channel=selected_channel;
                }
            }else if(page==1){
                if(y>=43 && y<=70){
                    log_close();
                    if(x<148){selected_profile=(selected_profile+1)%profile_count;}
                    else if(x<232){if(profiles[selected_profile].radio.channel)profiles[selected_profile].radio.channel--;}
                    else{if(profiles[selected_profile].radio.channel<83)profiles[selected_profile].radio.channel++;}
                    select_rx();
                }else if(y>=181 && y<=208){
                    if(x<124)log_start(1);
                    else if(have_packet && radio_ready){
                        if(radio_send_once(&profiles[selected_profile].radio,last_packet,last_length)){
                            ++transmitted;strcpy(notice,"Packet transmitted, no ACK");
                        }else strcpy(notice,"Radio transmission failed");
                    }
                }
            }else if(page==2 && y>=64 && y<190){
                unsigned index=(y-64)/21;
                if(index<profile_count && index<6)selected_profile=index;
            }else if(page==3 && y>=66 && y<=93){
                if(x<160)mount_sd();
                else{run_calibration();save_calibration();recalibrated=1;}
            }
            if(page!=previous_page || recalibrated)draw_page();
            else refresh_controls();
            last_redraw=milliseconds();
        }
        if(page==0 && radio_ready && !paused){
            channel_hits[next_channel]+=radio_activity(next_channel);
            ++channel_samples[next_channel];
            if(++next_channel==84){
                next_channel=0;
                if(logging){
                    for(unsigned ch=0;ch<84 && logging;++ch){
                        char row[80];
                        int count=snprintf(row,sizeof(row),"%lu,%u,%u,%u,%u\r\n",(unsigned long)milliseconds(),ch,2400+ch,channel_hits[ch],channel_samples[ch]);
                        if(count>0 && count<(int)sizeof(row))log_bytes(row,count);
                    }
                }
            }
        }else if(page==1 && radio_ready)packet_received();
        else delay_ms(5);
        uint32_t now=milliseconds();
        if(logging && now-last_sync>=1000){
            if(f_sync(&logfile)!=FR_OK){log_close();sd_ready=0;strcpy(notice,"SD sync failed");}
            last_sync=now;
        }
        if(now-last_redraw>=500 && (page==0||page==1)){
            refresh_values();last_redraw=milliseconds();
        }
    }
}
