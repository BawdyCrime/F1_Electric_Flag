#include "flag_display.h"

#include "bsp_display.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "flag_display";

// Bottom flag color fill is a square matching the panel width.
static constexpr int32_t FLAG_SQUARE_SIZE = BSP_DISPLAY_PANEL_WIDTH;
static constexpr int32_t FLAG_NAME_BAR_HEIGHT = BSP_DISPLAY_PANEL_HEIGHT - FLAG_SQUARE_SIZE;
static constexpr uint32_t BLINK_PERIOD_MS = 500;
static constexpr uint32_t GREEN_AUTO_REVERT_MS = 30000; // 30 seconds

static lv_obj_t *s_lap_screen = nullptr;
static lv_obj_t *s_lap_label = nullptr;
static lv_obj_t *s_lap_label_shadow = nullptr;
static lv_obj_t *s_green_screen = nullptr;
static lv_obj_t *s_yellow_screen = nullptr;
static lv_obj_t *s_yellow_square = nullptr;
static lv_obj_t *s_yellow_name_label = nullptr;
static lv_obj_t *s_yellow_name_label_shadow = nullptr;
static lv_obj_t *s_double_yellow_screen = nullptr;
static lv_obj_t *s_double_yellow_square = nullptr;
static lv_obj_t *s_double_yellow_name_label = nullptr;
static lv_obj_t *s_double_yellow_name_label_shadow = nullptr;
static bool s_double_yellow_triangle_state = false;
static lv_timer_t *s_blink_timer = nullptr;
static lv_timer_t *s_double_yellow_blink_timer = nullptr;
static lv_timer_t *s_green_revert_timer = nullptr;
static uint32_t s_cached_current_lap = 0;
static uint32_t s_cached_total_laps = 0;
static flag_screen_t s_current_screen = FLAG_SCREEN_LAP;

static void blink_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_yellow_square == nullptr) {
        return;
    }
    lv_opa_t opa = lv_obj_get_style_bg_opa(s_yellow_square, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_yellow_square, opa == LV_OPA_COVER ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

// Draws one triangular half of the square each redraw; which half is picked by s_double_yellow_triangle_state.
static void double_yellow_draw_event_cb(lv_event_t *e) {
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_color_hex(0xFFD500);
    dsc.opa = LV_OPA_COVER;
    if (s_double_yellow_triangle_state) {
        dsc.p[0].x = area.x1;
        dsc.p[0].y = area.y1;
        dsc.p[1].x = area.x2;
        dsc.p[1].y = area.y1;
        dsc.p[2].x = area.x2;
        dsc.p[2].y = area.y2;
    } else {
        dsc.p[0].x = area.x1;
        dsc.p[0].y = area.y1;
        dsc.p[1].x = area.x1;
        dsc.p[1].y = area.y2;
        dsc.p[2].x = area.x2;
        dsc.p[2].y = area.y2;
    }
    lv_draw_triangle(layer, &dsc);
}

static void double_yellow_blink_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_double_yellow_square == nullptr) {
        return;
    }
    s_double_yellow_triangle_state = !s_double_yellow_triangle_state;
    lv_obj_invalidate(s_double_yellow_square);
}

// Shows the LAP screen; assumes the LVGL port lock is already held.
static void show_lap_locked(uint32_t current_lap, uint32_t total_laps) {
    s_cached_current_lap = current_lap;
    s_cached_total_laps = total_laps;
    lv_label_set_text_fmt(s_lap_label, "LAP\n%u/%u", (unsigned)current_lap, (unsigned)total_laps);
    lv_label_set_text_fmt(s_lap_label_shadow, "LAP\n%u/%u", (unsigned)current_lap, (unsigned)total_laps);
    lv_obj_center(s_lap_label);
    lv_obj_align(s_lap_label_shadow, LV_ALIGN_CENTER, 1, 0);
    lv_timer_pause(s_blink_timer);
    lv_timer_pause(s_double_yellow_blink_timer);
    lv_timer_pause(s_green_revert_timer);
    lv_scr_load(s_lap_screen);
    s_current_screen = FLAG_SCREEN_LAP;
}

static void green_revert_timer_cb(lv_timer_t *timer) {
    (void)timer;
    show_lap_locked(s_cached_current_lap, s_cached_total_laps);
}

// Creates a black screen with the flag name bar (top) already attached; the caller adds the bottom square.
static lv_obj_t *create_flag_screen_base(const char *flag_name, lv_obj_t **out_label, lv_obj_t **out_label_shadow) {
    lv_obj_t *screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);

    lv_obj_t *name_bar = lv_obj_create(screen);
    lv_obj_set_size(name_bar, BSP_DISPLAY_PANEL_WIDTH, FLAG_NAME_BAR_HEIGHT);
    lv_obj_align(name_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(name_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(name_bar, 0, 0);
    lv_obj_set_style_pad_all(name_bar, 0, 0);
    lv_obj_set_scrollbar_mode(name_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(name_bar, LV_OBJ_FLAG_SCROLLABLE);

    // LVGL has no bold weight for built-in fonts; fake it by drawing an offset copy behind the label.
    lv_obj_t *name_label_shadow = lv_label_create(name_bar);
    lv_obj_set_style_text_color(name_label_shadow, lv_color_white(), 0);
    lv_obj_set_style_text_font(name_label_shadow, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_align(name_label_shadow, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(name_label_shadow, flag_name);
    lv_obj_align(name_label_shadow, LV_ALIGN_CENTER, 1, 0);

    lv_obj_t *name_label = lv_label_create(name_bar);
    lv_obj_set_style_text_color(name_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(name_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_align(name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(name_label, flag_name);
    lv_obj_align(name_label, LV_ALIGN_CENTER, 0, 0);

    if (out_label != nullptr) {
        *out_label = name_label;
    }
    if (out_label_shadow != nullptr) {
        *out_label_shadow = name_label_shadow;
    }

    return screen;
}

// Creates a flag screen: black top bar with the flag name centered, square color fill at the bottom.
static lv_obj_t *create_flag_screen(uint32_t color_rgb888, const char *flag_name, lv_obj_t **out_square,
                                     lv_obj_t **out_label, lv_obj_t **out_label_shadow) {
    lv_obj_t *screen = create_flag_screen_base(flag_name, out_label, out_label_shadow);

    lv_obj_t *square = lv_obj_create(screen);
    lv_obj_set_size(square, FLAG_SQUARE_SIZE, FLAG_SQUARE_SIZE);
    lv_obj_align(square, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(square, lv_color_hex(color_rgb888), 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(square, 0, 0);
    lv_obj_set_style_radius(square, 0, 0);

    if (out_square != nullptr) {
        *out_square = square;
    }

    return screen;
}

// Creates the double yellow flag screen: same layout, but the square draws one triangle at a time (see blink timer).
static lv_obj_t *create_double_yellow_screen(const char *flag_name, lv_obj_t **out_square, lv_obj_t **out_label,
                                              lv_obj_t **out_label_shadow) {
    lv_obj_t *screen = create_flag_screen_base(flag_name, out_label, out_label_shadow);

    lv_obj_t *square = lv_obj_create(screen);
    lv_obj_set_size(square, FLAG_SQUARE_SIZE, FLAG_SQUARE_SIZE);
    lv_obj_align(square, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(square, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(square, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(square, 0, 0);
    lv_obj_set_style_radius(square, 0, 0);
    lv_obj_add_event_cb(square, double_yellow_draw_event_cb, LV_EVENT_DRAW_MAIN, nullptr);

    if (out_square != nullptr) {
        *out_square = square;
    }

    return screen;
}


esp_err_t flag_display_init(void) {
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    s_lap_screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_lap_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_lap_screen, LV_OPA_COVER, 0);

    // LVGL has no bold weight for built-in fonts; fake it by drawing an offset copy behind the label.
    s_lap_label_shadow = lv_label_create(s_lap_screen);
    lv_obj_set_style_text_color(s_lap_label_shadow, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_lap_label_shadow, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_lap_label_shadow, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lap_label_shadow, "LAP -/-");

    s_lap_label = lv_label_create(s_lap_screen);
    lv_obj_set_style_text_color(s_lap_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_lap_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_lap_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_lap_label, "LAP -/-");
    lv_obj_center(s_lap_label);
    lv_obj_align(s_lap_label_shadow, LV_ALIGN_CENTER, 1, 0);

    s_green_screen = create_flag_screen(0x00B140, "GREEN FLAG", nullptr, nullptr, nullptr);
    s_yellow_screen = create_flag_screen(0xFFD500, "YELLOW FLAG", &s_yellow_square, &s_yellow_name_label,
                                         &s_yellow_name_label_shadow);
    s_double_yellow_screen = create_double_yellow_screen("DOUBLE YELLOW FLAG", &s_double_yellow_square,
                                                          &s_double_yellow_name_label,
                                                          &s_double_yellow_name_label_shadow);

    s_blink_timer = lv_timer_create(blink_timer_cb, BLINK_PERIOD_MS, nullptr);
    lv_timer_pause(s_blink_timer);

    s_double_yellow_blink_timer = lv_timer_create(double_yellow_blink_timer_cb, BLINK_PERIOD_MS, nullptr);
    lv_timer_pause(s_double_yellow_blink_timer);

    s_green_revert_timer = lv_timer_create(green_revert_timer_cb, GREEN_AUTO_REVERT_MS, nullptr);
    lv_timer_pause(s_green_revert_timer);

    lvgl_port_unlock();
    return ESP_OK;
}

flag_screen_t flag_display_get_current_screen(void) {
    return s_current_screen;
}

esp_err_t flag_display_show_lap(uint32_t current_lap, uint32_t total_laps) {
    if (s_lap_screen == nullptr || s_lap_label == nullptr || s_lap_label_shadow == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    show_lap_locked(current_lap, total_laps);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_green(void) {
    if (s_green_screen == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    lv_timer_pause(s_blink_timer);
    lv_timer_pause(s_double_yellow_blink_timer);
    lv_timer_reset(s_green_revert_timer);
    lv_timer_resume(s_green_revert_timer);
    lv_scr_load(s_green_screen);
    s_current_screen = FLAG_SCREEN_GREEN;

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_yellow(const char *turn_info) {
    if (s_yellow_screen == nullptr || s_yellow_square == nullptr || s_yellow_name_label == nullptr ||
        s_yellow_name_label_shadow == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    const char *text = (turn_info != nullptr && turn_info[0] != '\0') ? turn_info : "YELLOW FLAG";
    lv_label_set_text(s_yellow_name_label, text);
    lv_label_set_text(s_yellow_name_label_shadow, text);

    lv_obj_set_style_bg_opa(s_yellow_square, LV_OPA_COVER, 0);
    lv_timer_reset(s_blink_timer);
    lv_timer_resume(s_blink_timer);
    lv_timer_pause(s_double_yellow_blink_timer);
    lv_timer_pause(s_green_revert_timer);
    lv_scr_load(s_yellow_screen);
    s_current_screen = FLAG_SCREEN_YELLOW;

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_double_yellow(const char *turn_info) {
    if (s_double_yellow_screen == nullptr || s_double_yellow_square == nullptr ||
        s_double_yellow_name_label == nullptr || s_double_yellow_name_label_shadow == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    const char *text = (turn_info != nullptr && turn_info[0] != '\0') ? turn_info : "DOUBLE YELLOW FLAG";
    lv_label_set_text(s_double_yellow_name_label, text);
    lv_label_set_text(s_double_yellow_name_label_shadow, text);

    lv_timer_pause(s_blink_timer);
    s_double_yellow_triangle_state = false;
    lv_timer_reset(s_double_yellow_blink_timer);
    lv_timer_resume(s_double_yellow_blink_timer);
    lv_timer_pause(s_green_revert_timer);
    lv_scr_load(s_double_yellow_screen);
    s_current_screen = FLAG_SCREEN_DOUBLE_YELLOW;

    lvgl_port_unlock();
    return ESP_OK;
}
