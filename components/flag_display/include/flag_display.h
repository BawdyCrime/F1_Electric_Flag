#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLAG_SCREEN_INTERVAL = 0, // default idle screen: driver interval/timing table
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

// One row of the interval screen's driver table.
typedef enum {
    FLAG_TEAM_RED_BULL_RACING,
    FLAG_TEAM_MCLAREN,
    FLAG_TEAM_FERRARI,
    FLAG_TEAM_MERCEDES,
    FLAG_TEAM_ASTON_MARTIN,
    FLAG_TEAM_ALPINE,
    FLAG_TEAM_WILLIAMS,
    FLAG_TEAM_RACING_BULLS,
    FLAG_TEAM_AUDI,
    FLAG_TEAM_HAAS,
    FLAG_TEAM_CADILLAC,
} flag_team_t;

typedef struct {
    uint8_t position;
    flag_team_t team;
    const char *driver_code; // 3-letter driver code
    const char *interval;    // pre-formatted gap text, e.g. "Interval" or "+0.842"
    char tyre;                // compound letter: S, M, H, I, W
} flag_interval_row_t;

// Creates the LVGL widgets used by all flag screens. Call once after bsp_display_init().
esp_err_t flag_display_init(void);

// Returns the currently shown screen, including changes made internally (e.g. green auto-revert).
flag_screen_t flag_display_get_current_screen(void);

// Shows the interval table (clipped to however many rows fit on screen) and updates the shared header.
esp_err_t flag_display_show_interval(uint32_t current_lap, uint32_t total_laps,
                                     const flag_interval_row_t *rows, size_t row_count);

// Updates the shared header race time from the remaining duration in seconds.
esp_err_t flag_display_update_race_time(uint32_t remaining_seconds);

// Shows a full-screen green flag, used briefly on any flag-clear transition.
esp_err_t flag_display_show_green(void);

// Shows a full-screen solid red flag until another screen is selected.
esp_err_t flag_display_show_red(void);

// Shows the yellow flag with a blinking color square.
esp_err_t flag_display_show_yellow(void);

// Shows the blue flag with a blinking color square.
esp_err_t flag_display_show_blue(void);

// Shows the double yellow flag; two triangles split the square and blink alternately.
esp_err_t flag_display_show_double_yellow(void);

// Shows the yellow Safety Car flag with a centered "SC" mark.
esp_err_t flag_display_show_safety_car(void);

// Shows the Virtual Safety Car flag with a centered "VSC" mark.
esp_err_t flag_display_show_vsc(void);

#ifdef __cplusplus
}
#endif
