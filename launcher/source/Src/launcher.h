#ifndef LAUNCHER_H
#define LAUNCHER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char name[32];
    char filename[13];
    uint32_t bytes;
    uint32_t crc32;
} LauncherEntry;

/* Packages live in 0:/APPS and have an 8.3 filename ending in .APP. */
bool launcher_list(unsigned page, LauncherEntry entries[5], unsigned *count,
                   bool *has_next);
bool launcher_installed(LauncherEntry *entry);
bool launcher_install(const char *filename,
                      void (*progress)(unsigned percent, const char *status));
const char *launcher_error(void);

/* Compatibility hook; direct handoff requires no automatic boot gate. */
void launcher_try_boot(void);
/* Returns false on validation/preparation failure; success does not return. */
bool launcher_request_boot(void);

#endif
