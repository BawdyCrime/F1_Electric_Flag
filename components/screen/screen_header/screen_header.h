#pragma once

#include "flag_view.h"
#include "lvgl.h"

#include <cstdint>

// Logo only; used by screens without lap/time rows.
void screen_header_create(lv_obj_t *parent);

// Creates view->screen with the logo, title (session name / lap) and time rows shared by the timing and flag screens.
void screen_header_create(flag_view_t *view, const char *title,
                          int32_t remaining_seconds);
void screen_header_update(flag_view_t *view, const char *title,
                          int32_t remaining_seconds);

// Header geometry in panel coordinates, for hit-testing touches.
bool screen_header_hit_header(int x, int y);
bool screen_header_hit_logo(int x, int y);
