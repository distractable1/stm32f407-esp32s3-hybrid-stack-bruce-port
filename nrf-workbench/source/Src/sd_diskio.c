#include "FatFs/ff.h"
#include "FatFs/diskio.h"
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))
#define PORTC 0x40020800u
#define PORTD 0x40020c00u

static DSTATUS card_status = STA_NOINIT;
static unsigned block_addressing;

static void gpio_mode(uint32_t port, unsigned pin, unsigned value)
{
    unsigned shift = pin * 2;
    REG(port) = (REG(port) & ~(3u << shift)) | (value << shift);
}

static void pin(uint32_t port, unsigned number, unsigned high)
{
    REG(port+0x18) = 1u << (number+(high ? 0 : 16));
}

static uint32_t cycles(void) { return REG(0xe0001004u); }

static unsigned expired(uint32_t start, unsigned ms)
{
    return (uint32_t)(cycles()-start) >= ms*16000u;
}

static uint8_t exchange(uint8_t value)
{
    uint8_t result = 0;
    for (unsigned mask = 128; mask; mask >>= 1) {
        pin(PORTD,2,value & mask);
        pin(PORTC,12,1);
        result = (result << 1) | ((REG(PORTC+0x10) >> 8) & 1);
        pin(PORTC,12,0);
    }
    return result;
}

static void deselect(void) { pin(PORTC,11,1); exchange(255); }

static unsigned ready(unsigned ms)
{
    uint32_t start = cycles();
    do { if (exchange(255) == 255) return 1; } while (!expired(start,ms));
    return 0;
}

static uint8_t command(unsigned cmd, uint32_t arg)
{
    deselect();
    pin(PORTC,11,0);
    if (!ready(500)) return 255;
    exchange(0x40 | cmd);
    exchange(arg >> 24); exchange(arg >> 16); exchange(arg >> 8); exchange(arg);
    exchange(cmd == 0 ? 0x95 : cmd == 8 ? 0x87 : 255);
    for (unsigned i = 0; i < 16; ++i) {
        uint8_t result = exchange(255);
        if (!(result & 128)) return result;
    }
    return 255;
}

static unsigned receive(uint8_t *data, unsigned length)
{
    uint32_t start = cycles();
    uint8_t token;
    do { token = exchange(255); } while (token == 255 && !expired(start,500));
    if (token != 0xfe) return 0;
    while (length--) *data++ = exchange(255);
    exchange(255); exchange(255);
    return 1;
}

DSTATUS disk_initialize(BYTE drive)
{
    if (drive) return STA_NOINIT;
    card_status = STA_NOINIT;
    /* DWT gives bounded timeouts, independent of SysTick polling. */
    REG(0xe000edfc) |= 1u << 24;
    REG(0xe0001000) |= 1;
    REG(0x40023830u) |= (1u << 2) | (1u << 3);
    pin(PORTC,11,1); pin(PORTC,12,0); pin(PORTD,2,1);
    gpio_mode(PORTC,11,1); gpio_mode(PORTC,12,1); gpio_mode(PORTD,2,1);
    gpio_mode(PORTC,8,0); gpio_mode(PORTC,9,0); gpio_mode(PORTC,10,0);
    REG(PORTC+0x0c) = (REG(PORTC+0x0c) & ~(3u << 16)) | (1u << 16);
    for (unsigned i = 0; i < 12; ++i) exchange(255);
    uint32_t start = cycles();
    uint8_t response;
    do { response = command(0,0); } while (response != 1 && !expired(start,1000));
    if (response != 1) { deselect(); return card_status; }
    unsigned version2 = command(8,0x1aa) == 1;
    if (version2) {
        uint8_t r7[4];
        for (unsigned i=0; i<4; ++i) r7[i]=exchange(255);
        if (r7[2] != 1 || r7[3] != 0xaa) { deselect(); return card_status; }
    }
    start = cycles();
    do {
        if (command(55,0) > 1) break;
        response = command(41,version2 ? 0x40000000u : 0);
    } while (response == 1 && !expired(start,2000));
    if (response != 0) { deselect(); return card_status; }
    block_addressing = 0;
    if (version2) {
        if (command(58,0)) { deselect(); return card_status; }
        uint8_t ocr = exchange(255);
        block_addressing = !!(ocr & 0x40);
        for (unsigned i=0; i<3; ++i) exchange(255);
    }
    if (!block_addressing && command(16,512)) { deselect(); return card_status; }
    deselect();
    card_status = 0;
    return card_status;
}

DSTATUS disk_status(BYTE drive) { return drive ? STA_NOINIT : card_status; }

DRESULT disk_read(BYTE drive, BYTE *data, LBA_t sector, UINT count)
{
    if (drive || !count) return RES_PARERR;
    if (card_status) return RES_NOTRDY;
    for (unsigned i=0; i<count; ++i) {
        uint32_t address = block_addressing ? sector+i : (sector+i)*512;
        if (command(17,address) || !receive(data+512*i,512)) { deselect(); card_status=STA_NOINIT; return RES_ERROR; }
        deselect();
    }
    return RES_OK;
}

DRESULT disk_write(BYTE drive, const BYTE *data, LBA_t sector, UINT count)
{
    if (drive || !count) return RES_PARERR;
    if (card_status) return RES_NOTRDY;
    for (unsigned i=0; i<count; ++i) {
        uint32_t address = block_addressing ? sector+i : (sector+i)*512;
        if (command(24,address)) { deselect(); card_status=STA_NOINIT; return RES_ERROR; }
        exchange(255); exchange(0xfe);
        for (unsigned j=0; j<512; ++j) exchange(data[512*i+j]);
        exchange(255); exchange(255);
        unsigned accepted = (exchange(255) & 31) == 5;
        unsigned complete = ready(1500);
        if (!accepted || !complete) { deselect(); card_status=STA_NOINIT; return RES_ERROR; }
        if (command(13,0) || exchange(255)) { deselect(); card_status=STA_NOINIT; return RES_ERROR; }
        deselect();
    }
    return RES_OK;
}

DRESULT disk_ioctl(BYTE drive, BYTE cmd, void *buffer)
{
    if (drive) return RES_PARERR;
    if (card_status) return RES_NOTRDY;
    DRESULT result = RES_OK;
    switch (cmd) {
    case CTRL_SYNC:
        pin(PORTC,11,0);
        if (!ready(1500)) result=RES_ERROR;
        break;
    case GET_SECTOR_SIZE:
        *(WORD *)buffer = 512;
        break;
    case GET_BLOCK_SIZE:
        *(DWORD *)buffer = 1;
        break;
    case GET_SECTOR_COUNT: {
        uint8_t csd[16];
        if (command(9,0) || !receive(csd,16)) { result=RES_ERROR; break; }
        if ((csd[0] >> 6) == 1) {
            uint32_t size = ((uint32_t)(csd[7]&63)<<16) | ((uint32_t)csd[8]<<8) | csd[9];
            *(LBA_t *)buffer = (size+1)*1024u;
        } else {
            unsigned n = (csd[5]&15) + ((csd[10]&128)>>7) + ((csd[9]&3)<<1) + 2;
            uint32_t size = ((csd[6]&3)<<10) | (csd[7]<<2) | (csd[8]>>6);
            if (n < 9 || n > 31) { result=RES_ERROR; break; }
            *(LBA_t *)buffer = (size+1) << (n-9);
        }
        break;
    }
    default: result=RES_PARERR; break;
    }
    deselect();
    return result;
}
