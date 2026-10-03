#include "screen_header.h"

#include "bsp_display.h"

static constexpr int32_t HEADER_HEIGHT = BSP_DISPLAY_PANEL_HEIGHT - BSP_DISPLAY_PANEL_WIDTH;
static constexpr int32_t LOGO_ROW_HEIGHT = 70;
static constexpr int32_t LAP_ROW_HEIGHT = 40;
static constexpr int32_t LOGO_TOP = 8;
static constexpr int32_t LOGO_WIDTH = 192;
static constexpr int32_t LOGO_HEIGHT = 48;

extern "C" const uint8_t _binary_f1_logo_192x48_rgb565_start[];

static const lv_image_dsc_t F1_LOGO_IMAGE = {
    .header = {
        .magic = LV_IMAGE_HEADER_MAGIC,
        .cf = LV_COLOR_FORMAT_RGB565,
        .flags = 0,
        .w = 192,
        .h = 48,
        .stride = 384,
        .reserved_2 = 0,
    },
    .data_size = 192 * 48 * 2,
    .data = _binary_f1_logo_192x48_rgb565_start,
    .reserved = nullptr,
    .reserved_2 = nullptr,
};

bool screen_header_hit_header(int x, int y)
{
    return x >= 0 && x < BSP_DISPLAY_PANEL_WIDTH && y >= 0 && y < HEADER_HEIGHT;
}

bool screen_header_hit_logo(int x, int y)
{
    const int left = (BSP_DISPLAY_PANEL_WIDTH - LOGO_WIDTH) / 2;
    return x >= left && x < left + LOGO_WIDTH && y >= LOGO_TOP && y < LOGO_TOP + LOGO_HEIGHT;
}

void screen_header_create(lv_obj_t *parent)
{
    lv_obj_t *logo = lv_image_create(parent);
    lv_image_set_src(logo, &F1_LOGO_IMAGE);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, LOGO_TOP);
}

static void update_lap_label(flag_view_t *view, const char *title) {
    lv_label_set_text(view->lap_header, title != nullptr ? title : "");
}

static void update_time_label(flag_view_t *view, uint32_t remaining_seconds) {
    const uint32_t hours = remaining_seconds / 3600U;
    const uint32_t minutes = (remaining_seconds / 60U) % 60U;
    const uint32_t seconds = remaining_seconds % 60U;
    lv_label_set_text_fmt(view->time_header, "%02u:%02u:%02u", (unsigned)hours, (unsigned)minutes,
                          (unsigned)seconds);
}

void screen_header_update(flag_view_t *view, const char *title,
                          uint32_t remaining_seconds) {
    update_lap_label(view, title);
    update_time_label(view, remaining_seconds);
}

void screen_header_create(flag_view_t *view, const char *title,
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

    screen_header_create(header);

    view->lap_header = lv_label_create(header);
    lv_obj_set_width(view->lap_header, BSP_DISPLAY_PANEL_WIDTH);
    lv_obj_set_style_text_color(view->lap_header, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->lap_header, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(view->lap_header, LV_TEXT_ALIGN_CENTER, 0);
    update_lap_label(view, title);
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