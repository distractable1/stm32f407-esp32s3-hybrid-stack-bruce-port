#ifndef TOOL_HID_H
#define TOOL_HID_H
#include <stdbool.h>
#include <stdint.h>
#include "tool_macro.h"

bool tool_hid_init(void);
void tool_hid_task(uint32_t now);
void tool_hid_shutdown(void);
bool tool_hid_connected(void);
const char *tool_hid_status(void);
/* Queued, US-layout ASCII typing; maximum 255 bytes. false means not accepted. */
bool tool_hid_text(const char *text);
/* A single queued relative report: dx/dy/wheel must fit -127..127, buttons 0..7.
 * Check false and retry rather than silently losing button transitions. */
bool tool_hid_mouse(int dx, int dy, uint8_t buttons, int wheel);
/* Cancel pending manual input and prioritize acknowledged all-up reports. */
void tool_hid_release(void);

/* Internal scheduler interface. Each key queues an acknowledged press/release
 * pair. Idle means all previously queued reports, including releases, finished. */
bool tool_hid_idle(void);
bool tool_hid_key(uint8_t modifiers, uint8_t key);
bool tool_hid_character(uint8_t character);
#endif
