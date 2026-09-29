#pragma once

#include "lvgl.h"

#include <stddef.h>

typedef struct {
    char pixel;
    lv_color_t color;
} dot_matrix_color_t;

void dot_matrix_draw(lv_layer_t *layer, const lv_area_t *area, const char *const *matrix, size_t rows,
                     size_t columns, const dot_matrix_color_t *palette, size_t palette_size);