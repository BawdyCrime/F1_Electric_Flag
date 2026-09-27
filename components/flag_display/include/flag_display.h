#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLAG_SCREEN_LAP = 0,   // default idle screen: full-screen "LAP x/y"
    FLAG_SCREEN_GREEN,     // full-screen green, shown briefly on flag clear
    FLAG_SCREEN_YELLOW,
    FLAG_SCREEN_DOUBLE_YELLOW,
    FLAG_SCREEN_BLUE,
    FLAG_SCREEN_RED,
    FLAG_SCREEN_SAFETY_CAR,
    FLAG_SCREEN_VSC,
    FLAG_SCREEN_CHEQUERED,
    FLAG_SCREEN_WHITE,
} flag_screen_t;

// Creates the LVGL widgets used by all flag screens. Call once after bsp_display_init().
esp_err_t flag_display_init(void);

// Returns the currently shown screen, including changes made internally (e.g. green auto-revert).
flag_screen_t flag_display_get_current_screen(void);

// Shows the default idle screen: full-screen "LAP current/total".
esp_err_t flag_display_show_lap(uint32_t current_lap, uint32_t total_laps);

// Shows a full-screen green flag, used briefly on any flag-clear transition.
esp_err_t flag_display_show_green(void);

// Shows the yellow flag; the color square blinks to draw attention.
// turn_info is optional (pass nullptr or an empty string to omit it), e.g. "TURN 6".
esp_err_t flag_display_show_yellow(const char *turn_info);

// Shows the double yellow flag; two triangles split the square and blink alternately.
// turn_info is optional (pass nullptr or an empty string to omit it), e.g. "TURN 6".
esp_err_t flag_display_show_double_yellow(const char *turn_info);

#ifdef __cplusplus
}
#endif
