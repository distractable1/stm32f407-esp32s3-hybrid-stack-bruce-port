#include "app.h"
#include "board_support.h"
#include <stdio.h>

/* The project factory replaces this token with the configured display name. */
const char app_display_name[] = "APP_DISPLAY_NAME";

static uint32_t counter;
static bool contact_down, add_highlight, reset_highlight, counter_dirty;

static bool inside(int x, int y, int left, int width)
{
    return x >= left && x < left + width && y >= 88 && y < 116;
}

static void draw_counter(void)
{
    char value[32];
    snprintf(value, sizeof value, "Count: %lu", (unsigned long)counter);
    rectangle(24, 137, 272, 32, BLACK);
    text(24, 145, value, CYAN);
    counter_dirty = false;
}

static void increment(void)
{
    if (counter != UINT32_MAX) ++counter;
    counter_dirty = true;
}

void app_init(void)
{
    /* This can also be a resume/repaint after calibration: keep the counter. */
    contact_down = add_highlight = reset_highlight = false;
    rectangle(0, APP_CONTENT_TOP, APP_SCREEN_WIDTH,
              APP_CONTENT_BOTTOM - APP_CONTENT_TOP, BLACK);
    text(16, 42, "Your application lives in app.c", WHITE);
    text(16, 62, "Tap a button or press KEY1", WHITE);
    panel_button(24, 88, 128, "+1", 0);
    panel_button(168, 88, 128, "RESET", 0);
    draw_counter();
    text(16, 184, board_was_inherited() ? "Launcher hardware inherited" : "Local board initialization", GREEN);
}

void app_update(uint32_t now)
{
    (void)now;
    if (counter_dirty) draw_counter();
}

void app_touch(int x, int y, bool pressed)
{
    bool add = pressed && inside(x, y, 24, 128);
    bool reset = pressed && inside(x, y, 168, 128);
    if (pressed && !contact_down) {
        if (add) increment();
        if (reset) { counter = 0; counter_dirty = true; }
    }
    if (add != add_highlight) {
        add_highlight = add;
        panel_button(24, 88, 128, "+1", add);
    }
    if (reset != reset_highlight) {
        reset_highlight = reset;
        panel_button(168, 88, 128, "RESET", reset);
    }
    contact_down = pressed;
}

void app_button(unsigned buttons)
{
    if (buttons & APP_BUTTON_KEY1) increment();
    if (buttons & APP_BUTTON_WK_UP) { counter = 0; counter_dirty = true; }
}

void app_shutdown(void)
{
    /* Close/sync your files and stop app-owned peripherals here. Do not reset
     * shared LCD/touch/SD pins or clocks. app_init() may run again afterward. */
}

/* USB/DSP apps may override the SDK's weak board_background() here. Keep that
 * hook short and nonblocking; it is also called from display and delay helpers.
 * Do not draw, delay, access FatFs, or call app_update() recursively from it. */
