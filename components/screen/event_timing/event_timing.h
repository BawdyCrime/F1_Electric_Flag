#pragma once

#include "flag_display.h"
#include "flag_view.h"

#include <cstddef>

// Number of event timing rows that fit in the panel area below the shared header.
extern const size_t EVENT_TIMING_MAX_ROWS;

void event_timing_create(flag_view_t *view);
void event_timing_update(flag_view_t *view, const event_timing_row_t *rows, size_t row_count);
