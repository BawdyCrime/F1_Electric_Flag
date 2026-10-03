#pragma once

#include "flag_view.h"
#include "lvgl.h"

#include <cstdint>

// Logo only; used by screens without lap/time rows.
void screen_header_create(lv_obj_t *parent);

// Creates view->screen with the logo, lap and time rows shared by the timing and flag screens.
void screen_header_create(flag_view_t *view, uint32_t current_lap, uint32_t total_laps,
                          uint32_t remaining_seconds);
void screen_header_update(flag_view_t *view, uint32_t current_lap, uint32_t total_laps,
                          uint32_t remaining_seconds);