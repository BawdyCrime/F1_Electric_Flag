#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FLAG_SCREEN_EVENT_TIMING = 0, // default idle screen: event timing table
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

// One row of the event timing screen's driver table.
typedef enum {
    F1_TEAM_RED_BULL_RACING,
    F1_TEAM_MCLAREN,
    F1_TEAM_FERRARI,
    F1_TEAM_MERCEDES,
    F1_TEAM_ASTON_MARTIN,
    F1_TEAM_ALPINE,
    F1_TEAM_WILLIAMS,
    F1_TEAM_RACING_BULLS,
    F1_TEAM_AUDI,
    F1_TEAM_HAAS,
    F1_TEAM_CADILLAC,
    F1_TEAM_UNKNOWN, // no logo, white accent
} f1_team_t;

// Lookup helpers for live/replay data. Matching is case-insensitive.
// Accepts OpenF1 team_name ("Red Bull Racing", "Haas F1 Team", "Kick Sauber"...) or short code ("RBR", "MCL"...).
f1_team_t f1_team_from_name(const char *name);
// Accepts OpenF1 team_colour hex (e.g. "3671C6", optional '#').
f1_team_t f1_team_from_colour(const char *hex);
// Short 3-letter team code, "---" for unknown.
const char *f1_team_code(f1_team_t team);

typedef struct {
    uint8_t position; // 0 when the driver's race position is not available yet
    f1_team_t team;
    const char *driver_code; // 3-letter driver code
    const char *interval;    // pre-formatted gap text, e.g. "Interval" or "+0.842"
    char tyre;                // compound letter: S, M, H, I, W
} event_timing_row_t;

// Creates the LVGL widgets used by all flag screens. Call once after bsp_display_init().
esp_err_t flag_display_init(void);

// Returns the currently shown screen, including changes made internally (e.g. green auto-revert).
flag_screen_t flag_display_get_current_screen(void);

// Header shows session_name, or "RACE - LAP x/y" when session_name is "Race".
// Shows the event timing table (clipped to however many rows fit on screen) and updates the shared header.
esp_err_t flag_display_show_event_timing(const char *session_name, uint32_t current_lap, uint32_t total_laps,
                                         const event_timing_row_t *rows, size_t row_count);

// Updates the timing table and shared header without changing the currently shown screen.
esp_err_t flag_display_update_event_timing(const char *session_name, uint32_t current_lap, uint32_t total_laps,
                                           const event_timing_row_t *rows, size_t row_count);

// Updates the shared header race time from the remaining duration in seconds.
esp_err_t flag_display_update_race_time(int32_t remaining_seconds);

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

// Shows the Safety Car flag with a blinking yellow edge and centered "SC" mark.
esp_err_t flag_display_show_safety_car(void);

// Shows the Virtual Safety Car flag with a blinking yellow edge and centered "VSC" mark.
esp_err_t flag_display_show_vsc(void);

#ifdef __cplusplus
}
#endif
