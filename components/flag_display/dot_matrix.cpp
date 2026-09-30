#include "dot_matrix.h"

#include <algorithm>

static void dot_matrix_draw_internal(lv_layer_t *layer, const lv_area_t *area, const char *const *matrix,
                                     size_t rows, size_t columns, dot_matrix_pixel_fn pixel_fn, void *user_data,
                                     const dot_matrix_color_t *palette, size_t palette_size) {
    if (layer == nullptr || area == nullptr || (matrix == nullptr && pixel_fn == nullptr) || rows == 0 ||
        columns == 0 || palette == nullptr || palette_size == 0) {
        return;
    }

    const int32_t width = lv_area_get_width(area);
    const int32_t height = lv_area_get_height(area);
    const int32_t pitch_x = width / static_cast<int32_t>(columns);
    const int32_t pitch_y = height / static_cast<int32_t>(rows);
    if (pitch_x <= 0 || pitch_y <= 0) {
        return;
    }

    const int32_t pitch = std::min(pitch_x, pitch_y);
    const int32_t gap = std::max<int32_t>(1, pitch / 5);
    const int32_t dot_size = std::max<int32_t>(1, pitch - gap);
    const int32_t grid_offset_x = (width - pitch_x * static_cast<int32_t>(columns)) / 2;
    const int32_t grid_offset_y = (height - pitch_y * static_cast<int32_t>(rows)) / 2;

    lv_draw_rect_dsc_t dot;
    lv_draw_rect_dsc_init(&dot);
    dot.bg_opa = LV_OPA_COVER;
    dot.border_width = 0;
    dot.radius = LV_RADIUS_CIRCLE;

    for (size_t row = 0; row < rows; ++row) {
        if (pixel_fn == nullptr && matrix[row] == nullptr) {
            continue;
        }
        for (size_t column = 0; column < columns; ++column) {
            const char pixel = pixel_fn != nullptr ? pixel_fn(row, column, user_data) : matrix[row][column];
            if (pixel == '.' || pixel == '\0') {
                continue;
            }

            bool found_color = false;
            for (size_t color_index = 0; color_index < palette_size; ++color_index) {
                if (palette[color_index].pixel == pixel) {
                    dot.bg_color = palette[color_index].color;
                    found_color = true;
                    break;
                }
            }
            if (!found_color) {
                continue;
            }

            const int32_t cell_x = area->x1 + grid_offset_x + static_cast<int32_t>(column) * pitch_x;
            const int32_t cell_y = area->y1 + grid_offset_y + static_cast<int32_t>(row) * pitch_y;
            const int32_t dot_margin_x = (pitch_x - dot_size) / 2;
            const int32_t dot_margin_y = (pitch_y - dot_size) / 2;
            lv_area_t dot_area = {cell_x + dot_margin_x, cell_y + dot_margin_y,
                                  cell_x + dot_margin_x + dot_size - 1,
                                  cell_y + dot_margin_y + dot_size - 1};
            lv_draw_rect(layer, &dot, &dot_area);
        }
    }
}

void dot_matrix_draw(lv_layer_t *layer, const lv_area_t *area, const char *const *matrix, size_t rows,
                     size_t columns, const dot_matrix_color_t *palette, size_t palette_size) {
    dot_matrix_draw_internal(layer, area, matrix, rows, columns, nullptr, nullptr, palette, palette_size);
}

void dot_matrix_draw_generated(lv_layer_t *layer, const lv_area_t *area, size_t rows, size_t columns,
                               dot_matrix_pixel_fn pixel_fn, void *user_data, const dot_matrix_color_t *palette,
                               size_t palette_size) {
    dot_matrix_draw_internal(layer, area, nullptr, rows, columns, pixel_fn, user_data, palette, palette_size);
}