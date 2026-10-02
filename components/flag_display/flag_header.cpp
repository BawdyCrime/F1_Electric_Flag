#include "flag_header.h"

#include "bsp_display.h"
#include "display_logo.h"

static constexpr int32_t HEADER_HEIGHT = BSP_DISPLAY_PANEL_HEIGHT - BSP_DISPLAY_PANEL_WIDTH;
static constexpr int32_t LOGO_ROW_HEIGHT = 70;
static constexpr int32_t LAP_ROW_HEIGHT = 40;

static void update_lap_label(flag_view_t *view, uint32_t current_lap, uint32_t total_laps) {
    lv_label_set_text_fmt(view->lap_header, "LAP %u/%u", (unsigned)current_lap, (unsigned)total_laps);
}

static void update_time_label(flag_view_t *view, uint32_t remaining_seconds) {
    const uint32_t hours = remaining_seconds / 3600U;
    const uint32_t minutes = (remaining_seconds / 60U) % 60U;
    const uint32_t seconds = remaining_seconds % 60U;
    lv_label_set_text_fmt(view->time_header, "%02u:%02u:%02u", (unsigned)hours, (unsigned)minutes,
                          (unsigned)seconds);
}

void flag_header_update(flag_view_t *view, uint32_t current_lap, uint32_t total_laps,
                        uint32_t remaining_seconds) {
    update_lap_label(view, current_lap, total_laps);
    update_time_label(view, remaining_seconds);
}

void flag_header_create(flag_view_t *view, uint32_t current_lap, uint32_t total_laps,
                        uint32_t remaining_seconds) {
    view->screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(view->screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(view->screen, 0, 0);
    lv_obj_set_style_border_width(view->screen, 0, 0);

    lv_obj_t *header = lv_obj_create(view->screen);
    lv_obj_set_size(header, BSP_DISPLAY_PANEL_WIDTH, HEADER_HEIGHT);
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(header, 0, 0);
    lv_obj_set_style_pad_all(header, 0, 0);
    lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scrollable(header, false);

    display_logo_create(header);

    view->lap_header = lv_label_create(header);
    lv_obj_set_width(view->lap_header, BSP_DISPLAY_PANEL_WIDTH);
    lv_obj_set_style_text_color(view->lap_header, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->lap_header, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(view->lap_header, LV_TEXT_ALIGN_CENTER, 0);
    update_lap_label(view, current_lap, total_laps);
    lv_obj_update_layout(view->lap_header);
    const lv_coord_t lap_label_height = lv_obj_get_height(view->lap_header);
    const lv_coord_t lap_label_top = LOGO_ROW_HEIGHT + (LAP_ROW_HEIGHT - lap_label_height) / 2;
    lv_obj_align(view->lap_header, LV_ALIGN_TOP_MID, 0, lap_label_top);

    view->time_header = lv_label_create(header);
    lv_obj_set_width(view->time_header, BSP_DISPLAY_PANEL_WIDTH);
    lv_obj_set_style_text_color(view->time_header, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->time_header, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(view->time_header, LV_TEXT_ALIGN_CENTER, 0);
    update_time_label(view, remaining_seconds);
    const int32_t time_row_top = LOGO_ROW_HEIGHT + LAP_ROW_HEIGHT;
    lv_obj_update_layout(view->time_header);
    const lv_coord_t time_label_height = lv_obj_get_height(view->time_header);
    const lv_coord_t time_label_top = time_row_top + (HEADER_HEIGHT - time_row_top - time_label_height) / 2;
    lv_obj_align(view->time_header, LV_ALIGN_TOP_MID, 0, time_label_top - 8);
}