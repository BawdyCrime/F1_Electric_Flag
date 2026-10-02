#pragma once

#include "lvgl.h"

#include <stddef.h>

typedef struct {
    char pixel;
    lv_color_t color;
} dot_matrix_color_t;

typedef char (*dot_matrix_pixel_fn)(size_t row, size_t column, void *user_data);

void dot_matrix_draw(lv_layer_t *layer, const lv_area_t *area, const char *const *matrix, size_t rows,
                     size_t columns, const dot_matrix_color_t *palette, size_t palette_size);
void dot_matrix_draw_generated(lv_layer_t *layer, const lv_area_t *area, size_t rows, size_t columns,
                               dot_matrix_pixel_fn pixel_fn, void *user_data, const dot_matrix_color_t *palette,
                               size_t palette_size);