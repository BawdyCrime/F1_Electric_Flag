#include "flag_panel.h"

#include "bsp_display.h"
#include "dot_matrix.h"

static constexpr int32_t FLAG_SQUARE_SIZE = BSP_DISPLAY_PANEL_WIDTH;

static void matrix_draw_event_cb(lv_event_t *event) {
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);

    dot_matrix_color_t palette[] = {
        {'G', lv_color_hex(0x00B140)},
        {'R', lv_color_hex(0xFF0000)},
        {'Y', lv_color_hex(0xFFD500)},
        {'B', lv_color_hex(0x0057B8)},
        {'W', lv_color_white()},
    };
    dot_matrix_draw_generated(layer, &area, MATRIX_PATTERN_SIZE, MATRIX_PATTERN_SIZE, matrix_pattern_get_pixel,
                              lv_event_get_user_data(event), palette, sizeof(palette) / sizeof(palette[0]));
}

void flag_panel_create(flag_view_t *view, matrix_pattern_t *pattern) {
    view->square = lv_obj_create(view->screen);
    lv_obj_set_size(view->square, FLAG_SQUARE_SIZE, FLAG_SQUARE_SIZE);
    lv_obj_align(view->square, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(view->square, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view->square, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->square, 0, 0);
    lv_obj_set_style_radius(view->square, 0, 0);
    lv_obj_add_event_cb(view->square, matrix_draw_event_cb, LV_EVENT_DRAW_MAIN, pattern);
}