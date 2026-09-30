#pragma once

#include "flag_view.h"

#include <cstdint>

void flag_header_create(flag_view_t *view, uint32_t current_lap, uint32_t total_laps,
                       uint32_t remaining_seconds);
void flag_header_update(flag_view_t *view, uint32_t current_lap, uint32_t total_laps,
                       uint32_t remaining_seconds);