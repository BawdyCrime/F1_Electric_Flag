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
static constexpr uint32_t GREEN_AUTO_REVERT_MS = 10000; // 10 seconds

struct flag_view_t {
    lv_obj_t *screen = nullptr;
    lv_obj_t *square = nullptr;
    lv_obj_t *name_label = nullptr;
    lv_obj_t *name_label_shadow = nullptr;
    lv_obj_t *detail_label = nullptr;
    lv_obj_t *detail_label_shadow = nullptr;
};

struct flag_display_state_t {
    flag_view_t lap;
    flag_view_t green;
    flag_view_t yellow;
    flag_view_t blue;
    flag_view_t double_yellow;
    lv_timer_t *flag_blink_timer = nullptr;
    lv_timer_t *double_yellow_blink_timer = nullptr;
    lv_timer_t *green_revert_timer = nullptr;
    bool double_yellow_triangle_state = false;
    uint32_t current_lap = 0;
    uint32_t total_laps = 0;
    flag_screen_t current_screen = FLAG_SCREEN_LAP;
};

static flag_display_state_t s_state;

static lv_obj_t *get_blinking_flag_square(void) {
    switch (s_state.current_screen) {
        case FLAG_SCREEN_YELLOW:
            return s_state.yellow.square;
        case FLAG_SCREEN_BLUE:
            return s_state.blue.square;
        default:
            return nullptr;
    }
}

static void blink_timer_cb(lv_timer_t *timer) {
    (void)timer;
    lv_obj_t *square = get_blinking_flag_square();
    if (square == nullptr) {
        return;
    }
    lv_opa_t opa = lv_obj_get_style_bg_opa(square, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(square, opa == LV_OPA_COVER ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
}

static void set_flag_name_and_detail(flag_view_t *view, const char *flag_name, const char *detail_prefix,
                                     const char *detail) {
    const bool has_detail = detail != nullptr && detail[0] != '\0';
    lv_label_set_text(view->name_label, flag_name);
    lv_label_set_text(view->name_label_shadow, flag_name);
    if (has_detail) {
        lv_label_set_text_fmt(view->detail_label, "%s%s", detail_prefix, detail);
        lv_label_set_text_fmt(view->detail_label_shadow, "%s%s", detail_prefix, detail);
    } else {
        lv_label_set_text(view->detail_label, "");
        lv_label_set_text(view->detail_label_shadow, "");
    }

    lv_obj_update_layout(view->screen);
    const lv_coord_t title_height = lv_obj_get_height(view->name_label);
    const lv_coord_t detail_height = has_detail ? lv_obj_get_height(view->detail_label) : 0;
    constexpr lv_coord_t DETAIL_GAP = 10;
    const lv_coord_t group_height = title_height + (has_detail ? detail_height + DETAIL_GAP : 0);
    const lv_coord_t group_top = (FLAG_NAME_BAR_HEIGHT - group_height) / 2;
    lv_obj_align(view->name_label, LV_ALIGN_TOP_MID, 0, group_top);
    lv_obj_align(view->name_label_shadow, LV_ALIGN_TOP_MID, 1, group_top);
    if (has_detail) {
        const lv_coord_t detail_top = group_top + title_height + DETAIL_GAP;
        lv_obj_align(view->detail_label, LV_ALIGN_TOP_MID, 0, detail_top);
        lv_obj_align(view->detail_label_shadow, LV_ALIGN_TOP_MID, 1, detail_top);
    }
}

// Draws one triangular half of the square each redraw; which half is picked by the display state.
static void double_yellow_draw_event_cb(lv_event_t *e) {
    lv_obj_t *obj = static_cast<lv_obj_t *>(lv_event_get_current_target(e));
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);

    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_color_hex(0xFFD500);
    dsc.opa = LV_OPA_COVER;
    if (s_state.double_yellow_triangle_state) {
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
    if (s_state.double_yellow.square == nullptr) {
        return;
    }
    s_state.double_yellow_triangle_state = !s_state.double_yellow_triangle_state;
    lv_obj_invalidate(s_state.double_yellow.square);
}

static void pause_flag_timers(void) {
    lv_timer_pause(s_state.flag_blink_timer);
    lv_timer_pause(s_state.double_yellow_blink_timer);
    lv_timer_pause(s_state.green_revert_timer);
}

static void load_screen_locked(lv_obj_t *screen, flag_screen_t screen_id) {
    lv_scr_load(screen);
    s_state.current_screen = screen_id;
}

// Shows the LAP screen; assumes the LVGL port lock is already held.
static void show_lap_locked(uint32_t current_lap, uint32_t total_laps) {
    s_state.current_lap = current_lap;
    s_state.total_laps = total_laps;
    lv_label_set_text_fmt(s_state.lap.name_label, "LAP\n%u/%u", (unsigned)current_lap, (unsigned)total_laps);
    lv_label_set_text_fmt(s_state.lap.name_label_shadow, "LAP\n%u/%u", (unsigned)current_lap,
                          (unsigned)total_laps);
    lv_obj_center(s_state.lap.name_label);
    lv_obj_align(s_state.lap.name_label_shadow, LV_ALIGN_CENTER, 1, 0);
    pause_flag_timers();
    load_screen_locked(s_state.lap.screen, FLAG_SCREEN_LAP);
}

static void green_revert_timer_cb(lv_timer_t *timer) {
    (void)timer;
    show_lap_locked(s_state.current_lap, s_state.total_laps);
}

// Creates a black screen with the flag name bar (top) already attached; the caller adds the bottom square.
static void create_flag_screen_base(const char *flag_name, flag_view_t *view) {
    view->screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(view->screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view->screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(view->screen, 0, 0);
    lv_obj_set_style_border_width(view->screen, 0, 0);

    lv_obj_t *name_bar = lv_obj_create(view->screen);
    lv_obj_set_size(name_bar, BSP_DISPLAY_PANEL_WIDTH, FLAG_NAME_BAR_HEIGHT);
    lv_obj_align(name_bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(name_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(name_bar, 0, 0);
    lv_obj_set_style_pad_all(name_bar, 0, 0);
    lv_obj_set_scrollbar_mode(name_bar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(name_bar, LV_OBJ_FLAG_SCROLLABLE);

    // LVGL has no bold weight for built-in fonts; fake it by drawing an offset copy behind the label.
    view->name_label_shadow = lv_label_create(name_bar);
    lv_obj_set_style_text_color(view->name_label_shadow, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->name_label_shadow, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_align(view->name_label_shadow, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(view->name_label_shadow, flag_name);
    lv_obj_align(view->name_label_shadow, LV_ALIGN_CENTER, 1, 0);

    view->name_label = lv_label_create(name_bar);
    lv_obj_set_style_text_color(view->name_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->name_label, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_align(view->name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(view->name_label, flag_name);
    lv_obj_align(view->name_label, LV_ALIGN_CENTER, 0, 0);

    view->detail_label_shadow = lv_label_create(name_bar);
    lv_obj_set_style_text_color(view->detail_label_shadow, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->detail_label_shadow, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(view->detail_label_shadow, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(view->detail_label_shadow, "");

    view->detail_label = lv_label_create(name_bar);
    lv_obj_set_style_text_color(view->detail_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->detail_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(view->detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(view->detail_label, "");
}

// Creates a flag screen: black top bar with the flag name centered, square color fill at the bottom.
static void create_flag_screen(uint32_t color_rgb888, const char *flag_name, flag_view_t *view) {
    create_flag_screen_base(flag_name, view);

    view->square = lv_obj_create(view->screen);
    lv_obj_set_size(view->square, FLAG_SQUARE_SIZE, FLAG_SQUARE_SIZE);
    lv_obj_align(view->square, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(view->square, lv_color_hex(color_rgb888), 0);
    lv_obj_set_style_bg_opa(view->square, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->square, 0, 0);
    lv_obj_set_style_radius(view->square, 0, 0);
}

// Creates the double yellow flag screen: same layout, but the square draws one triangle at a time (see blink timer).
static void create_double_yellow_screen(const char *flag_name, flag_view_t *view) {
    create_flag_screen_base(flag_name, view);

    view->square = lv_obj_create(view->screen);
    lv_obj_set_size(view->square, FLAG_SQUARE_SIZE, FLAG_SQUARE_SIZE);
    lv_obj_align(view->square, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(view->square, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view->square, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->square, 0, 0);
    lv_obj_set_style_radius(view->square, 0, 0);
    lv_obj_add_event_cb(view->square, double_yellow_draw_event_cb, LV_EVENT_DRAW_MAIN, nullptr);
}


esp_err_t flag_display_init(void) {
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    s_state.lap.screen = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(s_state.lap.screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_state.lap.screen, LV_OPA_COVER, 0);

    // LVGL has no bold weight for built-in fonts; fake it by drawing an offset copy behind the label.
    s_state.lap.name_label_shadow = lv_label_create(s_state.lap.screen);
    lv_obj_set_style_text_color(s_state.lap.name_label_shadow, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_state.lap.name_label_shadow, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_state.lap.name_label_shadow, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_state.lap.name_label_shadow, "LAP -/-");

    s_state.lap.name_label = lv_label_create(s_state.lap.screen);
    lv_obj_set_style_text_color(s_state.lap.name_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_state.lap.name_label, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_align(s_state.lap.name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(s_state.lap.name_label, "LAP -/-");
    lv_obj_center(s_state.lap.name_label);
    lv_obj_align(s_state.lap.name_label_shadow, LV_ALIGN_CENTER, 1, 0);

    create_flag_screen(0x00B140, "GREEN FLAG", &s_state.green);
    create_flag_screen(0xFFD500, "YELLOW FLAG", &s_state.yellow);
    create_flag_screen(0x0057B8, "BLUE FLAG", &s_state.blue);
    create_double_yellow_screen("DOUBLE\nYELLOW FLAG", &s_state.double_yellow);

    s_state.flag_blink_timer = lv_timer_create(blink_timer_cb, BLINK_PERIOD_MS, nullptr);
    lv_timer_pause(s_state.flag_blink_timer);

    s_state.double_yellow_blink_timer = lv_timer_create(double_yellow_blink_timer_cb, BLINK_PERIOD_MS, nullptr);
    lv_timer_pause(s_state.double_yellow_blink_timer);

    s_state.green_revert_timer = lv_timer_create(green_revert_timer_cb, GREEN_AUTO_REVERT_MS, nullptr);
    lv_timer_pause(s_state.green_revert_timer);

    lvgl_port_unlock();
    return ESP_OK;
}

flag_screen_t flag_display_get_current_screen(void) {
    return s_state.current_screen;
}

esp_err_t flag_display_show_lap(uint32_t current_lap, uint32_t total_laps) {
    if (s_state.lap.screen == nullptr || s_state.lap.name_label == nullptr ||
        s_state.lap.name_label_shadow == nullptr) {
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
    if (s_state.green.screen == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    pause_flag_timers();
    lv_timer_reset(s_state.green_revert_timer);
    lv_timer_resume(s_state.green_revert_timer);
    load_screen_locked(s_state.green.screen, FLAG_SCREEN_GREEN);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_yellow(const char *turn_info) {
    if (s_state.yellow.screen == nullptr || s_state.yellow.square == nullptr ||
        s_state.yellow.name_label == nullptr || s_state.yellow.name_label_shadow == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    set_flag_name_and_detail(&s_state.yellow, "YELLOW FLAG", "", turn_info);

    lv_obj_set_style_bg_opa(s_state.yellow.square, LV_OPA_COVER, 0);
    pause_flag_timers();
    lv_timer_reset(s_state.flag_blink_timer);
    lv_timer_resume(s_state.flag_blink_timer);
    load_screen_locked(s_state.yellow.screen, FLAG_SCREEN_YELLOW);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_blue(const char *car_number) {
    if (s_state.blue.screen == nullptr || s_state.blue.square == nullptr ||
        s_state.blue.name_label == nullptr || s_state.blue.name_label_shadow == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    set_flag_name_and_detail(&s_state.blue, "BLUE FLAG", "CAR ", car_number);

    lv_obj_set_style_bg_opa(s_state.blue.square, LV_OPA_COVER, 0);
    pause_flag_timers();
    lv_timer_reset(s_state.flag_blink_timer);
    lv_timer_resume(s_state.flag_blink_timer);
    load_screen_locked(s_state.blue.screen, FLAG_SCREEN_BLUE);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_double_yellow(const char *turn_info) {
    if (s_state.double_yellow.screen == nullptr || s_state.double_yellow.square == nullptr ||
        s_state.double_yellow.name_label == nullptr || s_state.double_yellow.name_label_shadow == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    set_flag_name_and_detail(&s_state.double_yellow, "DOUBLE\nYELLOW FLAG", "", turn_info);

    pause_flag_timers();
    s_state.double_yellow_triangle_state = false;
    lv_timer_reset(s_state.double_yellow_blink_timer);
    lv_timer_resume(s_state.double_yellow_blink_timer);
    load_screen_locked(s_state.double_yellow.screen, FLAG_SCREEN_DOUBLE_YELLOW);

    lvgl_port_unlock();
    return ESP_OK;
}
