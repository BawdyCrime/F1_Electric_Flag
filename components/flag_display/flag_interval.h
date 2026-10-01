#pragma once

#include "flag_display.h"
#include "flag_view.h"

#include <cstddef>

// Number of interval rows that fit in the panel area below the shared header.
extern const size_t FLAG_INTERVAL_MAX_ROWS;

void flag_interval_create(flag_view_t *view);
void flag_interval_update(flag_view_t *view, const flag_interval_row_t *rows, size_t row_count);
