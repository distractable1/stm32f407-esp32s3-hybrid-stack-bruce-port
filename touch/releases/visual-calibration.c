#include <stdint.h>
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
    /* SysTick polls at 1 kHz with the internal 16 MHz oscillator. */
    while (ms--) {
        REG(0xe000e018u) = 0;
        while (!(REG(0xe000e010u) & (1u << 16))) {}
    }
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
    COMMAND(0x36,0x28);
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
    for (int i = 0; i < width * height; ++i) lcd_write(color,1);
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
        while (!touching()) {}
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

void SystemInit(void)
{
    REG(0xe000ed88u) |= 0x00f00000u;
    __asm volatile ("dsb"); __asm volatile ("isb");
}

int main(void)
{
    REG(RCC) |= 1;
    while (!(REG(RCC) & 2)) {}
    REG(RCC+8) = 0;
    while (REG(RCC+8) & 12) {}
    REG(RCC+0x30) |= 0x7f;
    REG(RCC+0x44) |= 1u << 4;
    (void)REG(RCC+0x44);
    REG(0xe000e014u) = 15999;
    REG(0xe000e010u) = 5;
    REG(A+0x24) = (REG(A+0x24) & ~(15u << 4)) | (7u << 4);
    mode(A,9,2);
    REG(UART+8) = 139; REG(UART+0x0c) = (1u<<13) | (1u<<3);
    write_pin(C,13,1); write_pin(B,0,0); write_pin(F,11,0);
    mode(C,13,1); mode(B,0,1); mode(F,11,1); mode(B,1,0); mode(B,2,0);
    REG(B+0x0c) = (REG(B+0x0c) & ~(3u<<2)) | (1u<<2);
    serial("Visual calibration boot\r\n");
    if (!lcd_init()) {
        serial("Unknown LCD ID. Record the ID messages for diagnosis.\r\n");
        for (;;) {}
    }
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
    rectangle(0,0,320,240,BLACK);
    text(16,8,"TEST: DOT FOLLOWS STYLUS",WHITE);
    text(16,218,"Power cycle to recalibrate",WHITE);
    int oldx = -1, oldy = -1;
    unsigned tracking = 0;
    float filtered_x = 0, filtered_y = 0;
    for (;;) {
        if (touching()) {
            Point p = read_touch();
            float mapped_x = transform[0][0]*p.x/4095 + transform[0][1]*p.y/4095 + transform[0][2];
            float mapped_y = transform[1][0]*p.x/4095 + transform[1][1]*p.y/4095 + transform[1][2];
            /* Discard a sample if the stylus lifted during acquisition. */
            if (!touching()) {
                tracking = 0;
                continue;
            }
            unsigned new_contact = !tracking;
            if (!tracking) {
                filtered_x = mapped_x;
                filtered_y = mapped_y;
                tracking = 1;
            } else {
                filtered_x += 0.25f * (mapped_x-filtered_x);
                filtered_y += 0.25f * (mapped_y-filtered_y);
            }
            int x = filtered_x < 0 ? 0 : filtered_x > 319 ? 319 : filtered_x+0.5f;
            int y = filtered_y < 0 ? 0 : filtered_y > 239 ? 239 : filtered_y+0.5f;
            int dx = x-oldx, dy = y-oldy;
            if (oldx < 0 || new_contact || dx*dx + dy*dy > 4) {
                if (oldx >= 0) rectangle(oldx-2,oldy-2,5,5,BLACK);
                rectangle(x-2,y-2,5,5,GREEN);
                oldx = x; oldy = y;
            }
        } else {
            tracking = 0;
        }
        delay_ms(20);
    }
}
