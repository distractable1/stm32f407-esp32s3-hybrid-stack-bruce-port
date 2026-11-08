#include <stdint.h>
#include <stdio.h>
#include "tusb.h"
#include "synth_usb.h"
#include "calibration_store.h"
static unsigned usb_running;
static void service_usb(void) { if(usb_running) { tud_task_ext(0,false); synth_audio_task(); } }
#include "calibration-font.h"

#define REG(a) (*(volatile uint32_t *)(a))
#define A 0x40020000u
#define B 0x40020400u
#define C 0x40020800u
#define D 0x40020c00u
#define E 0x40021000u
#define F 0x40021400u
#define G 0x40021800u
#define RCC 0x40023800u
#define UART 0x40011000u
#define BLACK 0x0841
#define WHITE 0xffff
#define CYAN 0x07ff
#define GREEN 0x07e0
#define RED 0xf800

typedef struct { float x, y; } Point;
static const Point targets[5] = {{28,48},{28,190},{292,190},{292,48},{160,120}};
static Point raw[5];
static float transform[2][3];

static void delay_ms(unsigned ms)
{
    uint32_t start=tusb_time_millis_api();
    while((uint32_t)(tusb_time_millis_api()-start)<ms)service_usb();
}

static void mode(uint32_t port, unsigned pin, unsigned value)
{
    unsigned shift = 2 * pin;
    REG(port) = (REG(port) & ~(3u << shift)) | (value << shift);
}

static void write_pin(uint32_t port, unsigned pin, unsigned value)
{
    REG(port + 0x18) = 1u << (pin + (value ? 0 : 16));
}

static unsigned read_pin(uint32_t port, unsigned pin)
{
    return (REG(port + 0x10) >> pin) & 1u;
}

static void wait_bus(void)
{
    for (volatile unsigned i = 0; i < 8; ++i) __asm volatile ("nop");
}

static void serial(const char *text)
{
    while (*text) {
        while (!(REG(UART) & 0x80)) {}
        REG(UART + 4) = (unsigned char)*text++;
    }
}

static void number(int value)
{
    char digits[11];
    unsigned count = 0;
    if (value < 0) { serial("-"); value = -value; }
    do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
    while (count) {
        char text[2] = {digits[--count], 0};
        serial(text);
    }
}

static unsigned spi(unsigned value, unsigned bits)
{
    unsigned result = 0;
    while (bits) {
        --bits;
        write_pin(F,11,(value >> bits) & 1);
        wait_bus();
        write_pin(B,0,1);
        wait_bus();
        result = (result << 1) | read_pin(B,2);
        write_pin(B,0,0);
    }
    return result;
}

static unsigned adc(unsigned command)
{
    write_pin(C,13,0);
    spi(command,8);
    unsigned value = spi(0,16);
    write_pin(C,13,1);
    return (value >> 3) & 4095;
}

static unsigned touching(void) { return !read_pin(B,1); }

static unsigned median_five(unsigned samples[5])
{
    for (unsigned i = 1; i < 5; ++i) {
        unsigned value = samples[i];
        unsigned j = i;
        while (j > 0 && samples[j-1] > value) {
            samples[j] = samples[j-1];
            --j;
        }
        samples[j] = value;
    }
    return samples[2];
}

static Point read_touch(void)
{
    unsigned x[5], y[5];
    for (unsigned i = 0; i < 5; ++i) {
        x[i] = adc(0xd0);
        y[i] = adc(0x90);
    }
    Point p = {median_five(x), median_five(y)};
    return p;
}

/* The schematic uses a 16-bit 8080 bus on GPIOD/GPIOE. */
static void bus_mode(unsigned output)
{
    const uint32_t dm = 0xf03f000fu;
    const uint32_t em = 0xffffc000u;
    REG(D) = (REG(D) & ~dm) | (output ? (dm & 0x55555555u) : 0);
    REG(E) = (REG(E) & ~em) | (output ? (em & 0x55555555u) : 0);
}

static void bus_write(unsigned value)
{
    unsigned pd = ((value & 3) << 14) | ((value & 12) >> 2) | ((value & 0xe000) >> 5);
    unsigned pe = (value & 0x1ff0) << 3;
    REG(D + 0x18) = ((0xc703u & ~pd) << 16) | pd;
    REG(E + 0x18) = ((0xff80u & ~pe) << 16) | pe;
}

static unsigned bus_read(void)
{
    unsigned pd = REG(D + 0x10), pe = REG(E + 0x10);
    return ((pd >> 14) & 3) | ((pd & 3) << 2) | ((pe >> 3) & 0x1ff0) | ((pd & 0x700) << 5);
}

static void lcd_write(unsigned value, unsigned data)
{
    write_pin(F,12,data);
    bus_write(value);
    write_pin(G,12,0);
    write_pin(D,5,0);
    wait_bus();
    write_pin(D,5,1);
    write_pin(G,12,1);
}

static unsigned lcd_read(void)
{
    write_pin(F,12,1);
    write_pin(G,12,0);
    write_pin(D,4,0);
    wait_bus();
    unsigned result = bus_read();
    write_pin(D,4,1);
    write_pin(G,12,1);
    return result;
}

static void command(unsigned cmd, const uint8_t *values, unsigned length)
{
    lcd_write(cmd,0);
    for (unsigned i = 0; i < length; ++i) lcd_write(values[i],1);
}

#define COMMAND(cmd, ...) do { const uint8_t bytes[] = {__VA_ARGS__}; command(cmd,bytes,sizeof(bytes)); } while (0)

static unsigned identify(unsigned cmd)
{
    lcd_write(cmd,0);
    bus_mode(0);
    lcd_read();
    unsigned value = 0;
    for (unsigned i = 0; i < 3; ++i) value = (value << 8) | (lcd_read() & 255);
    bus_mode(1);
    return value;
}

static unsigned lcd_init(void)
{
    write_pin(D,4,1); write_pin(D,5,1); write_pin(G,12,1);
    mode(D,4,1); mode(D,5,1); mode(G,12,1); mode(F,12,1);
    write_pin(B,15,0); mode(B,15,1);
    bus_mode(1);
    delay_ms(150);
    unsigned id = identify(0xd3);
    serial("LCD D3 decimal="); number(id); serial("\r\n");
    unsigned ili = (id & 0xffff) == 0x9341;
    unsigned st = 0;
    if (!ili) {
        id = identify(0x04);
        serial("LCD 04 decimal="); number(id); serial("\r\n");
        st = (id & 0xffff) == 0x8552;
    }
    if (!ili && !st) return 0;
    lcd_write(0x01,0); delay_ms(150);
    if (ili) {
        COMMAND(0xc0,0x23); COMMAND(0xc1,0x10);
        COMMAND(0xc5,0x3e,0x28); COMMAND(0xc7,0x86);
        COMMAND(0xb1,0x00,0x18); COMMAND(0xb6,0x08,0x82,0x27);
        COMMAND(0xf2,0x00); COMMAND(0x26,0x01);
    } else {
        COMMAND(0xb2,0x0c,0x0c,0x00,0x33,0x33);
        COMMAND(0xb7,0x35); COMMAND(0xbb,0x19); COMMAND(0xc0,0x2c);
        COMMAND(0xc2,0x01); COMMAND(0xc3,0x12); COMMAND(0xc4,0x20);
        COMMAND(0xc6,0x0f); COMMAND(0xd0,0xa4,0xa1);
        lcd_write(0x21,0);
    }
    COMMAND(0x3a,0x55);
    COMMAND(0x36,0xe8);
    lcd_write(0x11,0); delay_ms(150);
    lcd_write(0x29,0); delay_ms(20);
    write_pin(B,15,1);
    serial(ili ? "ILI9341 ready\r\n" : "ST7789 ready\r\n");
    return 1;
}

static void rectangle(int x, int y, int width, int height, unsigned color)
{
    if (x < 0) { width += x; x = 0; }
    if (y < 0) { height += y; y = 0; }
    if (x + width > 320) width = 320 - x;
    if (y + height > 240) height = 240 - y;
    if (width <= 0 || height <= 0) return;
    unsigned right = x + width - 1, bottom = y + height - 1;
    COMMAND(0x2a,x >> 8,x,right >> 8,right);
    COMMAND(0x2b,y >> 8,y,bottom >> 8,bottom);
    lcd_write(0x2c,0);
    for (int i = 0; i < width * height; ++i) {
        lcd_write(color,1);
        if((i & 127)==0) service_usb();
    }
}

static void text(int x, int y, const char *value, unsigned color)
{
    while (*value) {
        unsigned ch = (unsigned char)*value++;
        if (ch >= 32 && ch <= 126)
            for (unsigned row = 0; row < 14; ++row)
                for (unsigned col = 0; col < 8; ++col)
                    if (font[ch - 32][row] & (1u << col)) rectangle(x+col,y+row,1,1,color);
        x += 8;
    }
}

static void cross(int x, int y, unsigned color)
{
    rectangle(x-10,y,21,1,color);
    rectangle(x,y-10,1,21,color);
    rectangle(x-2,y-2,5,5,color);
}

static Point capture(void)
{
    for (;;) {
        while (!touching()) { service_usb(); }
        delay_ms(40);
        Point sum = {0,0};
        float minx = 4095, miny = 4095, maxx = 0, maxy = 0;
        unsigned valid = 1;
        for (unsigned i = 0; i < 16; ++i) {
            if (!touching()) { valid = 0; break; }
            Point p = read_touch();
            sum.x += p.x; sum.y += p.y;
            if (p.x < minx) minx = p.x;
            if (p.y < miny) miny = p.y;
            if (p.x > maxx) maxx = p.x;
            if (p.y > maxy) maxy = p.y;
            delay_ms(3);
        }
        if (valid && maxx-minx < 80 && maxy-miny < 80) {
            sum.x /= 16; sum.y /= 16;
            return sum;
        }
    }
}

/* Fit an affine transform from all five displayed pixel targets. */
static unsigned calibrate(void)
{
    float matrix[3][5] = {{0}};
    for (unsigned i = 0; i < 5; ++i) {
        float v[3] = {raw[i].x / 4095,raw[i].y / 4095,1};
        for (unsigned row = 0; row < 3; ++row) {
            for (unsigned col = 0; col < 3; ++col) matrix[row][col] += v[row]*v[col];
            matrix[row][3] += v[row]*targets[i].x;
            matrix[row][4] += v[row]*targets[i].y;
        }
    }
    for (unsigned col = 0; col < 3; ++col) {
        unsigned pivot = col;
        for (unsigned row = col+1; row < 3; ++row)
            if (matrix[row][col]*matrix[row][col] > matrix[pivot][col]*matrix[pivot][col]) pivot = row;
        if (matrix[pivot][col]*matrix[pivot][col] < 0.000001f) return 0;
        for (unsigned j = 0; j < 5; ++j) {
            float temp = matrix[col][j]; matrix[col][j] = matrix[pivot][j]; matrix[pivot][j] = temp;
        }
        float divisor = matrix[col][col];
        for (unsigned j = col; j < 5; ++j) matrix[col][j] /= divisor;
        for (unsigned row = 0; row < 3; ++row) {
            if (row == col) continue;
            float factor = matrix[row][col];
            for (unsigned j = col; j < 5; ++j) matrix[row][j] -= factor*matrix[col][j];
        }
    }
    for (unsigned axis = 0; axis < 2; ++axis)
        for (unsigned j = 0; j < 3; ++j) transform[axis][j] = matrix[j][3+axis];
    for (unsigned i = 0; i < 5; ++i) {
        float x = transform[0][0]*raw[i].x/4095 + transform[0][1]*raw[i].y/4095 + transform[0][2];
        float y = transform[1][0]*raw[i].x/4095 + transform[1][1]*raw[i].y/4095 + transform[1][2];
        float dx = x-targets[i].x, dy = y-targets[i].y;
        if (dx*dx + dy*dy > 144) return 0;
    }
    return 1;
}


static void run_calibration(void)
{
    do {
        for (unsigned i = 0; i < 5; ++i) {
            while (touching()) delay_ms(10);
            rectangle(0,0,320,240,BLACK);
            text(16,8,"TOUCH CALIBRATION",WHITE);
            text(16,214,"Hold stylus on the cross",WHITE);
            char step[] = "1/5"; step[0] += i;
            text(280,8,step,CYAN);
            cross(targets[i].x,targets[i].y,CYAN);
            raw[i] = capture();
            cross(targets[i].x,targets[i].y,GREEN);
            serial("Target "); number(i+1); serial(" raw "); number(raw[i].x); serial(","); number(raw[i].y); serial("\r\n");
            while (touching()) delay_ms(10);
            delay_ms(100);
        }
        if (calibrate()) break;
        rectangle(0,0,320,240,BLACK);
        text(16,90,"Calibration needs another try",RED);
        serial("Calibration rejected: residual exceeds 12 pixels or degenerate samples\r\n");
        delay_ms(2000);
    } while (1);
    serial("Calibration accepted; coefficients x1000, inputs raw/4095:\r\n");
    for (unsigned axis = 0; axis < 2; ++axis) {
        for (unsigned j = 0; j < 3; ++j) { number(transform[axis][j]*1000); serial(" "); }
        serial("\r\n");
    }
}

void SystemInit(void)
{
    REG(0xe000ed88u)|=0x00f00000u;
    __asm volatile ("dsb");__asm volatile ("isb");
}

static void panel_button(int x,int y,int width,const char *label,unsigned active)
{
    rectangle(x,y,width,28,active?0x2d7f:0x18e4);
    text(x+5,y+7,label,WHITE);
}

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

static void status_line(const char *value)
{
    static char previous[40];
    if(!value){for(unsigned i=0;i<40;++i)previous[i]=0;return;}
    unsigned length=0;
    while(value[length] && length<38)++length;
    for(unsigned i=0;i<38;++i){
        unsigned ch=i<length?(unsigned char)value[i]:' ';
        if(previous[i]==(char)ch)continue;
        previous[i]=ch;
        unsigned x=8+i*8,y=119;
        COMMAND(0x2a,x>>8,x,(x+7)>>8,x+7);
        COMMAND(0x2b,0,y,0,y+13);
        lcd_write(0x2c,0);
        for(unsigned row=0;row<14;++row)
            for(unsigned col=0;col<8;++col)
                lcd_write((font[ch-32][row]&(1u<<col))?WHITE:BLACK,1);
        service_usb();
    }
}


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
    text(8,216,"WK_UP: return to keyboard",WHITE);
}
static void redraw_interface(void)
{
    status_line(0);
    if(effects_page)draw_effects();
    else draw_synth();
}
static void buttons_init(void)
{
    mode(E,3,0);mode(E,4,0);mode(A,0,0);
    REG(E+0x0c)=(REG(E+0x0c)&~((3u<<6)|(3u<<8)))|(1u<<6)|(1u<<8);
    REG(A+0x0c)=(REG(A+0x0c)&~3u)|2u;
}
static unsigned physical_buttons(void)
{
    static uint32_t started[3];
    static unsigned fired[3];
    unsigned inputs[3]={!read_pin(E,4),!read_pin(E,3),read_pin(A,0)};
    unsigned events=0;
    uint32_t now=tusb_time_millis_api();
    for(unsigned i=0;i<3;++i){
        if(!inputs[i]){started[i]=0;fired[i]=0;continue;}
        if(!started[i])started[i]=now+1;
        unsigned duration=i==0?900:60;
        if(!fired[i] && (uint32_t)(now-(started[i]-1))>=duration){
            fired[i]=1;events|=1u<<i;
        }
    }
    return events;
}
int main(void)
{
    if(!synth_clock_init())for(;;){}
    REG(RCC+0x30)|=0x7f;
    REG(RCC+0x44)|=1u<<4;
    (void)REG(RCC+0x44);
    REG(A+0x24)=(REG(A+0x24)&~(15u<<4))|(7u<<4);
    mode(A,9,2);
    REG(UART+8)=729;REG(UART+0x0c)=(1u<<13)|(1u<<3);
    write_pin(C,13,1);write_pin(B,0,0);write_pin(F,11,0);
    mode(C,13,1);mode(B,0,1);mode(F,11,1);mode(B,1,0);mode(B,2,0);
    REG(B+0x0c)=(REG(B+0x0c)&~(3u<<2))|(1u<<2);
    buttons_init();
    serial("USB synth engine boot\r\n");
    if(!lcd_init()){serial("LCD identification failed\r\n");for(;;){}}
    text(8,90,"Loading calibration from SD...",WHITE);
    bool calibrated=calibration_load(&transform[0][0]);
    serial(calibrated?"Loaded SD calibration\r\n":"No valid saved calibration\r\n");
    synth_usb_init();
    serial(synth_usb_initialized?"USB controller initialized\r\n":"USB controller initialization FAILED\r\n");
    usb_running=synth_usb_initialized;
    if(!calibrated){
        run_calibration();
        bool saved=calibration_save(&transform[0][0]);
        rectangle(0,212,320,28,BLACK);
        text(8,216,saved?"Calibration saved to SD":"SD save failed; using RAM",saved?GREEN:RED);
        serial(saved?"Saved calibration to SD\r\n":"Calibration SD save failed\r\n");
        delay_ms(1000);
    }
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
                run_calibration();
                bool saved=calibration_save(&transform[0][0]);
                rectangle(0,212,320,28,BLACK);
                text(8,216,saved?"Calibration saved to SD":"SD save failed; using RAM",saved?GREEN:RED);
                delay_ms(1000);
                redraw_interface();
            }
            if(buttons&2)synth_play();
            if(buttons&4){effects_page=!effects_page;redraw_interface();}
        }
        int key=-1;
        if(touching()){
            Point p=read_touch();
            float px=transform[0][0]*p.x/4095+transform[0][1]*p.y/4095+transform[0][2];
            float py=transform[1][0]*p.x/4095+transform[1][1]*p.y/4095+transform[1][2];
            if(touching() && px>=0 && px<320 && py>=0 && py<240){
                int x=px,y=py;
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

