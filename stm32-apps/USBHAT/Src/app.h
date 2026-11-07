#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>

/* main.c owns the top bar and footer. App drawing stays inside these bounds. */
#define APP_CONTENT_TOP 30
#define APP_CONTENT_BOTTOM 212
#define APP_SCREEN_WIDTH 320

/* These match the existing board_support physical_buttons() bit assignments. */
#define APP_BUTTON_KEY1 (1u << 1)
#define APP_BUTTON_WK_UP (1u << 2)

extern const char app_display_name[];

/* Called at startup and after calibration. Open resources and repaint the app;
 * preserve user state when resuming after app_shutdown(). */
void app_init(void);
/* Frequent foreground work. now is a wrapping millisecond clock. */
void app_update(uint32_t now);
/* pressed=true means press or movement; false releases the last coordinates.
 * Repeated identical coordinates produce no callback. Only app-area contacts
 * are delivered; leaving the app area releases the contact. */
void app_touch(int x, int y, bool pressed);
/* Debounced press bits only. A long WK_UP hold also emits its initial press. */
void app_button(unsigned buttons);
/* Close/sync files and stop app-owned hardware before calibration or return. */
void app_shutdown(void);

#endif
