#include "launcher.h"
#include "FatFs/ff.h"
#include "stm32f4xx.h"
#include <stddef.h>
#include <string.h>

#define APP_BASE       UINT32_C(0x08020000)
#define APP_FOOTER     UINT32_C(0x080fffc0)
#define APP_MAGIC      UINT32_C(0x31505041)
#define APP_VERSION    UINT32_C(1)
#define APP_BOARD      UINT32_C(0x54313030)
#define APP_COMMIT     UINT32_C(0x434f4d54)
#define APP_MAX_BYTES  (APP_FOOTER - APP_BASE)
#define FLASH_ERRORS  (FLASH_SR_OPERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR | \
                       FLASH_SR_PGPERR | FLASH_SR_PGSERR)
#define RAMFUNC __attribute__((section(".RamFunc"), noinline))

/* The package header and installed footer use exactly the same wire layout.
 * STM32F4 is little endian; package generators must emit little-endian u32s. */
typedef struct {
    uint32_t magic, version, board, load, bytes, crc32;
    char name[32];
    uint32_t header_crc32, commit;
} AppHeader;
_Static_assert(sizeof(AppHeader) == 64, "APP header size");
_Static_assert(offsetof(AppHeader, header_crc32) == 56, "APP header checksum");
_Static_assert(offsetof(AppHeader, commit) == 60, "APP commit offset");

static FATFS volume;
static uint8_t transfer[4096] __attribute__((aligned(4)));
static bool api_busy;
static const char *last_error = "";

/* Root-owned board support snapshots the live LCD/touch/SD configuration in
 * the 256-byte handoff area excluded from both projects' normal RAM. */
extern bool board_prepare_handoff(void);

static bool fail(const char *message)
{
    last_error = message;
    return false;
}

const char *launcher_error(void) { return last_error; }

static uint32_t crc_update(uint32_t crc, const void *data, uint32_t length)
{
    const uint8_t *bytes = data;
    while (length--) {
        crc ^= *bytes++;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
    }
    return crc;
}

static uint32_t crc32(const void *data, uint32_t length)
{
    return ~crc_update(UINT32_MAX, data, length);
}

static bool header_valid(const AppHeader *header)
{
    if (header->magic != APP_MAGIC || header->version != APP_VERSION ||
        header->board != APP_BOARD || header->load != APP_BASE ||
        header->commit != APP_COMMIT)
        return fail("Package format or board mismatch");
    if (header->bytes < 8 || header->bytes > APP_MAX_BYTES)
        return fail("Application does not fit its flash slot");
    if (header->header_crc32 != crc32(header, 56))
        return fail("Package header checksum failed");
    const char *end = memchr(header->name, 0, sizeof(header->name));
    if (!end || end == header->name)
        return fail("Package name is invalid");
    for (const char *p = header->name; p != end; ++p)
        if ((unsigned char)*p < 32 || (unsigned char)*p > 126)
            return fail("Package name contains invalid characters");
    return true;
}

static uint32_t little32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static bool vectors_valid(uint32_t stack, uint32_t reset, uint32_t bytes)
{
    bool stack_ok = (stack > UINT32_C(0x20000000) && stack <= UINT32_C(0x2001ff00)) ||
                    (stack > UINT32_C(0x10000000) && stack <= UINT32_C(0x10010000));
    uint32_t address = reset & ~UINT32_C(1);
    if (!stack_ok || (stack & 7u) || !(reset & 1u) ||
        address < APP_BASE + 8u || address >= APP_BASE + bytes)
        return fail("Application vectors are invalid");
    return true;
}

static void entry_from_header(LauncherEntry *entry, const AppHeader *header,
                              const char *filename)
{
    memset(entry, 0, sizeof(*entry));
    memcpy(entry->name, header->name, sizeof(entry->name));
    if (filename) {
        size_t length = strlen(filename);
        if (length < sizeof(entry->filename))
            memcpy(entry->filename, filename, length + 1);
    }
    entry->bytes = header->bytes;
    entry->crc32 = header->crc32;
}

static bool filename_valid(const char *name)
{
    if (!name) return false;
    unsigned length = 0, dot = 0;
    while (length < 13 && name[length]) {
        unsigned char c = (unsigned char)name[length];
        if (c == '.') {
            if (dot || !length || length > 8) return false;
            dot = length;
        } else if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
        ++length;
    }
    return length <= 12 && dot && length == dot + 4 &&
           (name[dot + 1] == 'A' || name[dot + 1] == 'a') &&
           (name[dot + 2] == 'P' || name[dot + 2] == 'p') &&
           (name[dot + 3] == 'P' || name[dot + 3] == 'p');
}

static bool mount_card(void)
{
    if (f_mount(&volume, "0:", 1) == FR_OK) return true;
    f_mount(NULL, "0:", 0);
    return fail("Cannot mount SD card");
}

static bool open_package(FIL *file, const char *filename, AppHeader *header)
{
    char path[24] = "0:/APPS/";
    if (!filename_valid(filename)) return fail("Use an 8.3 .APP filename");
    memcpy(path + 8, filename, strlen(filename) + 1);
    if (f_open(file, path, FA_READ) != FR_OK)
        return fail("Cannot open application package");
    UINT got = 0;
    bool ok = f_read(file, header, sizeof(*header), &got) == FR_OK &&
              got == sizeof(*header);
    if (!ok) fail("Cannot read package header");
    else if (!header_valid(header)) ok = false;
    else if (f_size(file) != sizeof(*header) + header->bytes)
        ok = fail("Package length does not match its header");
    if (!ok) f_close(file);
    return ok;
}

bool launcher_list(unsigned page, LauncherEntry entries[5], unsigned *count,
                   bool *has_next)
{
    if (!entries || !count || !has_next) return fail("Invalid browser request");
    *count = 0;
    *has_next = false;
    if (api_busy) return fail("Installer is busy");
    api_busy = true;
    last_error = "";
    bool result = false, directory_open = false;
    DIR directory;
    if (!mount_card()) goto done;
    FRESULT status = f_opendir(&directory, "0:/APPS");
    if (status != FR_OK) { fail("Create an APPS folder on the SD card"); goto done; }
    directory_open = true;
    uint64_t skip = (uint64_t)page * 5u;
    for (;;) {
        FILINFO info;
        status = f_readdir(&directory, &info);
        if (status != FR_OK) { fail("Cannot read application directory"); goto done; }
        if (!info.fname[0]) break;
        if ((info.fattrib & AM_DIR) || !filename_valid(info.fname)) continue;
        FIL file;
        AppHeader header;
        if (!open_package(&file, info.fname, &header)) continue;
        if (f_close(&file) != FR_OK) { fail("Cannot close application package"); goto done; }
        if (skip) { --skip; continue; }
        if (*count == 5) { *has_next = true; break; }
        entry_from_header(&entries[*count], &header, info.fname);
        ++*count;
    }
    result = true;
    last_error = "";
done:
    if (directory_open && f_closedir(&directory) != FR_OK && result)
        result = fail("Cannot close application directory");
    f_mount(NULL, "0:", 0);
    api_busy = false;
    return result;
}

static bool installed_header(AppHeader *header)
{
    memcpy(header, (const void *)APP_FOOTER, sizeof(*header));
    if (!header_valid(header)) return false;
    const uint32_t *vectors = (const uint32_t *)APP_BASE;
    if (!vectors_valid(vectors[0], vectors[1], header->bytes)) return false;
    if (crc32((const void *)APP_BASE, header->bytes) != header->crc32)
        return fail("Installed application checksum failed");
    return true;
}

bool launcher_installed(LauncherEntry *entry)
{
    AppHeader header;
    last_error = "";
    if (!installed_header(&header)) return false;
    if (entry) entry_from_header(entry, &header, NULL);
    return true;
}

/* DWT timeouts keep working while IRQs are masked. No flash-resident calls,
 * lookup tables, or interrupt handlers are needed while the flash is busy. */
static RAMFUNC bool flash_wait(uint32_t timeout_cycles)
{
    uint32_t start = DWT->CYCCNT;
    while (FLASH->SR & FLASH_SR_BSY)
        if ((uint32_t)(DWT->CYCCNT - start) >= timeout_cycles) return false;
    return !(FLASH->SR & FLASH_ERRORS);
}

static RAMFUNC bool flash_erase(unsigned sector, uint32_t timeout_cycles)
{
    if (sector < 5 || sector > 11) return false;
    if (!flash_wait(timeout_cycles)) return false;
    FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_SNB | FLASH_CR_PG |
                               FLASH_CR_SER | FLASH_CR_MER)) |
                FLASH_CR_PSIZE_1 | FLASH_CR_SER | (sector << FLASH_CR_SNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    bool result = flash_wait(timeout_cycles);
    FLASH->CR &= ~(FLASH_CR_SER | FLASH_CR_SNB);
    return result;
}

static RAMFUNC bool flash_word(uint32_t address, uint32_t value,
                               uint32_t timeout_cycles)
{
    if ((address & 3u) || address < APP_BASE || address > APP_FOOTER + 60u)
        return false;
    if (!flash_wait(timeout_cycles)) return false;
    FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    FLASH->CR = (FLASH->CR & ~(FLASH_CR_PSIZE | FLASH_CR_PG | FLASH_CR_SER |
                               FLASH_CR_SNB | FLASH_CR_MER)) |
                FLASH_CR_PSIZE_1 | FLASH_CR_PG;
    *(volatile uint32_t *)address = value;
    bool result = flash_wait(timeout_cycles);
    FLASH->CR &= ~FLASH_CR_PG;
    return result;
}

static void invalidate_flash_cache(void)
{
    uint32_t control = FLASH->ACR;
    FLASH->ACR = control & ~(FLASH_ACR_ICEN | FLASH_ACR_DCEN);
    FLASH->ACR |= FLASH_ACR_ICRST | FLASH_ACR_DCRST;
    FLASH->ACR &= ~(FLASH_ACR_ICRST | FLASH_ACR_DCRST);
    FLASH->ACR = control;
    __DSB();
    __ISB();
}

static bool erase_sector(unsigned sector, uint32_t timeout_cycles)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    bool result = flash_erase(sector, timeout_cycles);
    __set_PRIMASK(mask);
    return result;
}

static bool program_bytes(uint32_t address, const uint8_t *bytes, unsigned count,
                           uint32_t timeout_cycles)
{
    for (unsigned offset = 0; offset < count; offset += 4) {
        uint32_t word = UINT32_MAX;
        unsigned remaining = count - offset;
        if (remaining > 4) remaining = 4;
        memcpy(&word, bytes + offset, remaining);
        if (word == UINT32_MAX) continue;
        uint32_t mask = __get_PRIMASK();
        __disable_irq();
        bool result = flash_word(address + offset, word, timeout_cycles);
        __set_PRIMASK(mask);
        if (!result) return false;
    }
    return true;
}

static void report(void (*progress)(unsigned, const char *), unsigned percent,
                    const char *status)
{
    if (progress) progress(percent, status);
}

bool launcher_install(const char *filename,
                      void (*progress)(unsigned, const char *))
{
    if (api_busy) return fail("Installer is busy");
    api_busy = true;
    last_error = "";
    FIL file;
    bool file_open = false, flash_open = false, result = false;
    AppHeader header;
    report(progress, 0, "Checking package");
    if (!mount_card() || !open_package(&file, filename, &header)) goto done;
    file_open = true;
    uint32_t remaining = header.bytes, crc = UINT32_MAX, checked = 0;
    while (remaining) {
        UINT wanted = remaining > sizeof(transfer) ? sizeof(transfer) : remaining;
        UINT got = 0;
        if (f_read(&file, transfer, wanted, &got) != FR_OK || got != wanted) {
            fail("Cannot read complete application"); goto done;
        }
        if (!checked && !vectors_valid(little32(transfer), little32(transfer + 4), header.bytes))
            goto done;
        crc = crc_update(crc, transfer, got);
        remaining -= got;
        checked += got;
        report(progress, checked * 10u / header.bytes, "Checking package");
    }
    if (~crc != header.crc32) { fail("Package payload checksum failed"); goto done; }
    /* Re-selecting the already installed app does not consume erase cycles. */
    AppHeader previous;
    if (installed_header(&previous) && !memcmp(&previous, &header, sizeof(header))) {
        result = true;
        last_error = "";
        goto done;
    }
    last_error = "";
    if (f_lseek(&file, sizeof(header)) != FR_OK) { fail("Cannot rewind package"); goto done; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    uint32_t erase_timeout = (uint32_t)((uint64_t)SystemCoreClock * 5u);
    uint32_t program_timeout = SystemCoreClock / 10u;
    if (!erase_timeout || !program_timeout) { fail("Invalid system clock"); goto done; }
    FLASH->SR = FLASH_ERRORS | FLASH_SR_EOP;
    if (FLASH->CR & FLASH_CR_LOCK) {
        FLASH->KEYR = UINT32_C(0x45670123);
        FLASH->KEYR = UINT32_C(0xcdef89ab);
    }
    flash_open = true;
    if (FLASH->CR & FLASH_CR_LOCK) { fail("Cannot unlock application flash"); goto done; }
    /* Erase the commit-containing sector first. Any subsequent interruption
     * leaves the independently bootable launcher with an invalid app slot. */
    report(progress, 10, "Erasing application");
    if (!erase_sector(11, erase_timeout)) { fail("Cannot erase application footer"); goto done; }
    invalidate_flash_cache();
    if (*(volatile uint32_t *)(APP_FOOTER + 60u) != UINT32_MAX) {
        fail("Previous application was not invalidated"); goto done;
    }
    unsigned last_sector = 5u + (header.bytes - 1u) / UINT32_C(0x20000);
    for (unsigned sector = 5; sector <= last_sector && sector <= 10; ++sector) {
        if (!erase_sector(sector, erase_timeout)) { fail("Cannot erase application sector"); goto done; }
        report(progress, 10u + (sector - 4u) * 2u, "Erasing application");
    }
    uint32_t written = 0;
    while (written < header.bytes) {
        UINT wanted = header.bytes - written;
        if (wanted > sizeof(transfer)) wanted = sizeof(transfer);
        UINT got = 0;
        if (f_read(&file, transfer, wanted, &got) != FR_OK || got != wanted) {
            fail("SD read failed during installation"); goto done;
        }
        if (!program_bytes(APP_BASE + written, transfer, got, program_timeout)) {
            fail("Cannot program application"); goto done;
        }
        written += got;
        report(progress, 25u + written * 65u / header.bytes, "Programming application");
    }
    invalidate_flash_cache();
    report(progress, 92, "Verifying flash");
    if (crc32((const void *)APP_BASE, header.bytes) != header.crc32) {
        fail("Flash read-back checksum failed"); goto done;
    }
    const uint32_t *vectors = (const uint32_t *)APP_BASE;
    if (!vectors_valid(vectors[0], vectors[1], header.bytes)) goto done;
    /* A read-only SD file must close successfully before committing validity. */
    if (f_close(&file) != FR_OK) { file_open = false; fail("Cannot close package"); goto done; }
    file_open = false;
    report(progress, 98, "Committing application");
    if (!program_bytes(APP_FOOTER, (const uint8_t *)&header, 60, program_timeout)) {
        fail("Cannot program application metadata"); goto done;
    }
    invalidate_flash_cache();
    if (memcmp((const void *)APP_FOOTER, &header, 60)) {
        fail("Application metadata verification failed"); goto done;
    }
    if (!program_bytes(APP_FOOTER + 60u, (const uint8_t *)&header.commit, 4, program_timeout)) {
        fail("Cannot commit application"); goto done;
    }
    invalidate_flash_cache();
    if (!installed_header(&previous)) goto done;
    result = true;
    last_error = "";
done:
    if (flash_open) {
        FLASH->CR &= ~(FLASH_CR_PG | FLASH_CR_SER | FLASH_CR_SNB);
        FLASH->CR |= FLASH_CR_LOCK;
        invalidate_flash_cache();
    }
    if (file_open && f_close(&file) != FR_OK && result)
        result = fail("Cannot close package");
    f_mount(NULL, "0:", 0);
    api_busy = false;
    if (result) report(progress, 100, "Application ready");
    return result;
}

static void __attribute__((naked, noreturn))
jump_to_app(uint32_t stack __attribute__((unused)), uint32_t reset __attribute__((unused)))
{
    __asm volatile (
        "movs r2, #0\n"
        "msr basepri, r2\n"
        "msr faultmask, r2\n"
        "msr control, r2\n"
        "msr msp, r0\n"
        "dsb\n"
        "isb\n"
        "cpsie i\n"
        "bx r1\n");
}

void launcher_try_boot(void) { }

bool launcher_request_boot(void)
{
    if (__get_IPSR() != 0) return fail("Launch must run in thread mode");
    if (api_busy) return fail("Installer is busy");
    AppHeader header;
    if (!installed_header(&header)) return false;
    if (!board_prepare_handoff()) return fail("Cannot prepare board handoff");
    uint32_t stack = *(const uint32_t *)APP_BASE;
    uint32_t reset = *(const uint32_t *)(APP_BASE + 4u);
    __disable_irq();
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL = 0;
    for (unsigned index = 0; index < 8; ++index) {
        NVIC->ICER[index] = UINT32_MAX;
        NVIC->ICPR[index] = UINT32_MAX;
    }
    SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
    FLASH->CR |= FLASH_CR_LOCK;
    SCB->VTOR = APP_BASE;
    __DSB();
    __ISB();
    jump_to_app(stack, reset);
}
