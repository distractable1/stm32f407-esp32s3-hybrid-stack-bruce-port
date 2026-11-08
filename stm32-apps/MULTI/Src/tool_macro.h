#ifndef TOOL_MACRO_H
#define TOOL_MACRO_H
#include <stdbool.h>
#include <stdint.h>
#define TOOL_MACRO_MAX_BYTES 8192u

/* Copies and validates the complete script. Loading always disarms old input. */
bool tool_macro_load(const char *text, unsigned length);
bool tool_macro_arm(void);
/* Caller must require the user's physical confirmation before this call. */
bool tool_macro_run(void);
void tool_macro_cancel(void);
bool tool_macro_busy(void);
bool tool_macro_armed(void);
unsigned tool_macro_line(void);
const char *tool_macro_status(void);

/* Called by the HID transport only; these never access GUI or filesystem. */
void tool_macro_task(uint32_t now);
void tool_macro_transport_lost(void);
#endif
