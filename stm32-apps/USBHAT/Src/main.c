#include "board_support.h"
#include "app.h"
#include <stdio.h>

static bool contact_active;
static int contact_x, contact_y;

static void release_contact(void)
{
    if (contact_active) {
        contact_active = false;
        app_touch(contact_x, contact_y, false);
    }
}

static void draw_framework(void)
{
    char title[26];
    snprintf(title, sizeof title, "%.25s", app_display_name);
    rectangle(0, 0, 320, APP_CONTENT_TOP, BLACK);
    text(8, 8, title, CYAN);
    panel_button(224, 0, 88, "MENU", 0);
    rectangle(0, APP_CONTENT_BOTTOM, 320, 240 - APP_CONTENT_BOTTOM, BLACK);
    text(8, 217, "Hold KEY0: CAL | Hold WK_UP: MENU", WHITE);
}

static void return_to_menu(void)
{
    release_contact();
    app_shutdown();
    board_return_to_launcher();
}

int main(void)
{
    if (!board_init()) for (;;) { board_background(); }
    app_init();
    draw_framework();

    /* Ignore the touch that launched this app until the stylus is lifted. */
    bool wait_for_release = true;
    for (;;) {
        board_background();
        unsigned buttons = physical_buttons();
        if (buttons & (1u << 3)) return_to_menu();
        if (buttons & 1u) {
            release_contact();
            app_shutdown();
            board_recalibrate();
            app_init();
            draw_framework();
            wait_for_release = true;
            /* A calibration operation consumes this batch of button events. */
            buttons = 0;
        }
        buttons &= APP_BUTTON_KEY1 | APP_BUTTON_WK_UP;
        if (buttons) app_button(buttons);

        int x, y;
        bool pressed = board_touch(&x, &y);
        if (wait_for_release) {
            if (!pressed) wait_for_release = false;
        } else if (pressed && y < APP_CONTENT_TOP && x >= 224) {
            return_to_menu();
        } else if (pressed && y >= APP_CONTENT_TOP && y < APP_CONTENT_BOTTOM) {
            if (!contact_active || x != contact_x || y != contact_y) {
                contact_x = x; contact_y = y; contact_active = true;
                app_touch(x, y, true);
            }
        } else {
            release_contact();
        }

        app_update(tusb_time_millis_api());
        delay_ms(5);
    }
}
