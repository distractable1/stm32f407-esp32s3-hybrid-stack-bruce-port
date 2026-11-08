#include <stdint.h>

#define REG(address) (*(volatile uint32_t *)(address))
#define GPIOA 0x40020000u
#define GPIOB 0x40020400u
#define GPIOC 0x40020800u
#define GPIOF 0x40021400u
#define RCC 0x40023800u
#define USART1 0x40011000u

static void pin_mode(uint32_t port, unsigned pin, unsigned mode)
{
    unsigned shift = pin * 2;
    REG(port) = (REG(port) & ~(3u << shift)) | (mode << shift);
}

static void pin_write(uint32_t port, unsigned pin, unsigned high)
{
    REG(port + 0x18) = 1u << (pin + (high ? 0 : 16));
}

static unsigned pin_read(uint32_t port, unsigned pin)
{
    return (REG(port + 0x10) >> pin) & 1u;
}

static void delay_ticks(unsigned ticks)
{
    for (volatile unsigned i = 0; i < ticks; ++i)
        __asm volatile ("nop");
}

static unsigned touch_transfer(unsigned value, unsigned bits)
{
    unsigned result = 0;
    while (bits != 0) {
        --bits;
        pin_write(GPIOF, 11, (value >> bits) & 1u);
        delay_ticks(8);
        pin_write(GPIOB, 0, 1);
        delay_ticks(8);
        result = (result << 1) | pin_read(GPIOB, 2);
        pin_write(GPIOB, 0, 0);
    }
    return result;
}

static unsigned touch_read(unsigned command)
{
    pin_write(GPIOC, 13, 0);
    touch_transfer(command, 8);
    unsigned sample = touch_transfer(0, 16);
    pin_write(GPIOC, 13, 1);
    return (sample >> 3) & 0xfff;
}

static void uart_put(char ch)
{
    while (!(REG(USART1) & (1u << 7))) {}
    REG(USART1 + 4) = (unsigned char)ch;
}

static void uart_text(const char *text)
{
    while (*text)
        uart_put(*text++);
}

static void uart_number(unsigned value)
{
    char digits[5];
    unsigned count = 0;
    do {
        digits[count++] = '0' + value % 10;
        value /= 10;
    } while (value);
    while (count)
        uart_put(digits[--count]);
}

void reset_handler(void)
{
    /* Use the 16 MHz internal oscillator and unscaled peripheral clocks. */
    REG(RCC) |= 1;
    while (!(REG(RCC) & 2)) {}
    REG(RCC + 8) = 0;
    while (REG(RCC + 8) & 12) {}

    REG(RCC + 0x30) |= 0x27;
    REG(RCC + 0x44) |= 1u << 4;
    (void)REG(RCC + 0x44);

    /* USART1 TX, PA9, connected to the board's CH340 USB-UART. */
    REG(GPIOA + 0x24) = (REG(GPIOA + 0x24) & ~(15u << 4)) | (7u << 4);
    pin_mode(GPIOA, 9, 2);
    REG(USART1 + 8) = 139;
    REG(USART1 + 0x0c) = (1u << 13) | (1u << 3);

    pin_write(GPIOC, 13, 1);
    pin_write(GPIOB, 0, 0);
    pin_write(GPIOF, 11, 0);
    pin_mode(GPIOC, 13, 1);
    pin_mode(GPIOB, 0, 1);
    pin_mode(GPIOF, 11, 1);
    pin_mode(GPIOB, 1, 0);
    pin_mode(GPIOB, 2, 0);
    REG(GPIOB + 0x0c) = (REG(GPIOB + 0x0c) & ~(3u << 2)) | (1u << 2);

    uart_text("Touch probe ready. Tap with stylus.\r\n");
    for (;;) {
        if (!pin_read(GPIOB, 1)) {
            unsigned x = touch_read(0xd0);
            unsigned y = touch_read(0x90);
            uart_text("X=");
            uart_number(x);
            uart_text(" Y=");
            uart_number(y);
            uart_text("\r\n");
        }
        delay_ticks(80000);
    }
}

__attribute__((section(".vectors"), used))
const uintptr_t vectors[] = { 0x20004000u, (uintptr_t)reset_handler };
