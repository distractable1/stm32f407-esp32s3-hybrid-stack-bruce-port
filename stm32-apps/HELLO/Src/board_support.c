#include "board_support.h"
#include "calibration_store.h"
#include "stm32f4xx.h"
#include "calibration-font.h"
#include <stdio.h>
#include <string.h>
uint32_t SystemCoreClock=16000000;
static volatile uint32_t ticks;
uint32_t tusb_time_millis_api(void){return ticks;}
void SysTick_Handler(void){++ticks;}
__attribute__((weak)) void board_background(void){}
#define service_usb board_background
extern uint32_t g_pfnVectors[];
void SystemInit(void){
    SCB->CPACR|=0x00f00000u;
    SCB->VTOR=(uint32_t)g_pfnVectors;
    __DSB();__ISB();
}
extern uint32_t sd_state_export(void);
extern void sd_state_import(uint32_t state);
typedef struct {
    uint32_t magic,version,clock_hz,sd_state,lcd_id,rotation;
    float coefficients[6];
    uint32_t checksum;
} BoardHandoff;
_Static_assert(sizeof(BoardHandoff)==52,"Board handoff layout");
static bool inherited;
static uint32_t handoff_checksum(const BoardHandoff *h){
    const uint8_t *p=(const uint8_t*)h;uint32_t hash=2166136261u;
    for(unsigned i=0;i<sizeof(*h)-4;++i)hash=(hash^p[i])*16777619u;
    return hash;
}
#define REG(a) (*(volatile uint32_t *)(a))
#define A 0x40020000u
#define B 0x40020400u
#define C 0x40020800u
#define D 0x40020c00u
#define E 0x40021000u
#define F 0x40021400u
#define G 0x40021800u
#define RCC_BASE_ADDR 0x40023800u
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
static unsigned board_lcd_id;

void delay_ms(unsigned ms)
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

void serial(const char *text)
{
    while (*text) {
        while (!(REG(UART) & 0x80)) {}
        REG(UART + 4) = (unsigned char)*text++;
    }
}

void number(int value)
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
    board_lcd_id=ili?0x9341:0;
    unsigned st = 0;
    if (!ili) {
        id = identify(0x04);
        serial("LCD 04 decimal="); number(id); serial("\r\n");
        st = (id & 0xffff) == 0x8552;
        if(st)board_lcd_id=0x8552;
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

void rectangle(int x, int y, int width, int height, unsigned color)
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

void text(int x, int y, const char *value, unsigned color)
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

static bool board_clock_init(void)
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

static void buttons_init(void)
{
    mode(E,3,0);mode(E,4,0);mode(A,0,0);
    REG(E+0x0c)=(REG(E+0x0c)&~((3u<<6)|(3u<<8)))|(1u<<6)|(1u<<8);
    REG(A+0x0c)=(REG(A+0x0c)&~3u)|2u;
}
unsigned physical_buttons(void)
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
        if(i==2 && fired[i]==1 && (uint32_t)(now-(started[i]-1))>=1500){fired[i]=2;events|=8;}
    }
    return events;
}

static bool coefficients_valid(const float *p){
    for(unsigned i=0;i<6;++i)if(!(p[i]>=-2000 && p[i]<=2000))return false;
    float determinant=p[0]*p[4]-p[1]*p[3];
    return determinant>1000 || determinant< -1000;
}
static bool inherit_board(void){
    volatile uint8_t *p=(volatile uint8_t*)BOARD_HANDOFF_ADDRESS;
    BoardHandoff h;uint8_t *dst=(uint8_t*)&h;
    for(unsigned i=0;i<sizeof(h);++i)dst[i]=p[i];
    *(volatile uint32_t*)BOARD_HANDOFF_ADDRESS=0;__DMB();
    if(h.magic!=0x31444842 || h.version!=1 || h.clock_hz!=168000000 ||
       h.rotation!=0xe8 || (h.lcd_id!=0x9341 && h.lcd_id!=0x8552) ||
       h.checksum!=handoff_checksum(&h) || !coefficients_valid(h.coefficients) ||
       (RCC->CFGR&RCC_CFGR_SWS)!=RCC_CFGR_SWS_PLL || !(RCC->CR&RCC_CR_PLLRDY))return false;
    memcpy(transform,h.coefficients,sizeof(transform));
    board_lcd_id=h.lcd_id;SystemCoreClock=h.clock_hz;
    sd_state_import(h.sd_state);
    SysTick_Config(SystemCoreClock/1000);
    NVIC_SetPriority(SysTick_IRQn,2);
    return true;
}
bool board_was_inherited(void){return inherited;}
bool board_prepare_handoff(void){
    if(!coefficients_valid(&transform[0][0]))return false;
    BoardHandoff h={.magic=0x31444842,.version=1,.clock_hz=SystemCoreClock,
      .sd_state=sd_state_export(),.lcd_id=board_lcd_id,.rotation=0xe8};
    memcpy(h.coefficients,transform,sizeof(transform));h.checksum=handoff_checksum(&h);
    volatile uint8_t *p=(volatile uint8_t*)BOARD_HANDOFF_ADDRESS;
    const uint8_t *src=(const uint8_t*)&h;
    *(volatile uint32_t*)BOARD_HANDOFF_ADDRESS=0;
    for(unsigned i=4;i<sizeof(h);++i)p[i]=src[i];
    __DMB();*(volatile uint32_t*)BOARD_HANDOFF_ADDRESS=h.magic;__DSB();
    return true;
}
void board_recalibrate(void){
    run_calibration();
    bool saved=calibration_save(&transform[0][0]);
    rectangle(0,212,320,28,BLACK);
    text(8,216,saved?"Calibration saved to SD":"SD save failed; using RAM",saved?GREEN:RED);
    serial(saved?"Saved calibration to SD\r\n":"Calibration SD save failed\r\n");
    delay_ms(1000);
}
bool board_init(void){
    if((uint32_t)g_pfnVectors==0x08020000u && inherit_board()){
        inherited=true;serial("Inherited LCD, touch, SD and calibration\r\n");return true;
    }
    *(volatile uint32_t*)BOARD_HANDOFF_ADDRESS=0;
    if(!board_clock_init())return false;
    REG(RCC_BASE_ADDR+0x30)|=0x7f;
    REG(RCC_BASE_ADDR+0x44)|=1u<<4;(void)REG(RCC_BASE_ADDR+0x44);
    REG(A+0x24)=(REG(A+0x24)&~(15u<<4))|(7u<<4);mode(A,9,2);
    REG(UART+8)=729;REG(UART+0x0c)=(1u<<13)|(1u<<3);
    write_pin(C,13,1);write_pin(B,0,0);write_pin(F,11,0);
    mode(C,13,1);mode(B,0,1);mode(F,11,1);mode(B,1,0);mode(B,2,0);
    REG(B+0x0c)=(REG(B+0x0c)&~(3u<<2))|(1u<<2);
    buttons_init();
    if(!lcd_init()){serial("LCD identification failed\r\n");return false;}
    text(8,90,"Loading calibration from SD...",WHITE);
    bool calibrated=calibration_load(&transform[0][0]);
    serial(calibrated?"Loaded SD calibration\r\n":"No valid saved calibration\r\n");
    if(!calibrated)board_recalibrate();
    return true;
}
bool board_touch(int *x,int *y){
    if(!touching())return false;
    Point p=read_touch();
    float px=transform[0][0]*p.x/4095+transform[0][1]*p.y/4095+transform[0][2];
    float py=transform[1][0]*p.x/4095+transform[1][1]*p.y/4095+transform[1][2];
    if(!touching() || !(px>=0 && px<320 && py>=0 && py<240))return false;
    *x=px;*y=py;return true;
}
void panel_button(int x,int y,int width,const char *label,unsigned active){
    rectangle(x,y,width,28,active?0x2d7f:0x18e4);text(x+5,y+7,label,WHITE);
}
void board_status_line(int x,int y,const char *value,unsigned fg,unsigned bg){
    static char previous[40];static int previous_x=-1,previous_y=-1;
    if(!value){memset(previous,0,sizeof(previous));previous_x=-1;return;}
    if(x!=previous_x || y!=previous_y){memset(previous,0,sizeof(previous));previous_x=x;previous_y=y;}
    unsigned length=0;while(value[length] && length<38)++length;
    for(unsigned i=0;i<38 && x+(int)i*8<320;++i){
        unsigned ch=i<length?(unsigned char)value[i]:' ';
        if(ch<32 || ch>126)ch='?';
        if(previous[i]==(char)ch)continue;
        previous[i]=ch;unsigned colx=x+i*8;
        COMMAND(0x2a,colx>>8,colx,(colx+7)>>8,colx+7);
        COMMAND(0x2b,y>>8,y,(y+13)>>8,y+13);lcd_write(0x2c,0);
        for(unsigned row=0;row<14;++row)for(unsigned col=0;col<8;++col)
            lcd_write((font[ch-32][row]&(1u<<col))?fg:bg,1);
        board_background();
    }
}
void board_return_to_launcher(void){
    __disable_irq();__DSB();SCB->AIRCR=0x05fa0004u;__DSB();for(;;){}
}
