#include "flag_display.h"

#include "bsp_display.h"
#include "dot_matrix.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "lvgl.h"
#include "matrix_pattern.h"

#include <cstdint>

static const char *TAG = "flag_display";

// Bottom flag color fill is a square matching the panel width.
static constexpr int32_t FLAG_SQUARE_SIZE = BSP_DISPLAY_PANEL_WIDTH;
static constexpr int32_t HEADER_HEIGHT = BSP_DISPLAY_PANEL_HEIGHT - FLAG_SQUARE_SIZE;
static constexpr int32_t LOGO_ROW_HEIGHT = 70;
static constexpr int32_t LAP_ROW_HEIGHT = 40;
static constexpr uint32_t BLINK_PERIOD_MS = 500;
static constexpr uint32_t GREEN_AUTO_REVERT_MS = 10000; // 10 seconds

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

struct flag_view_t {
    lv_obj_t *screen = nullptr;
    lv_obj_t *square = nullptr;
    lv_obj_t *lap_header = nullptr;
    lv_obj_t *time_header = nullptr;
};

struct flag_display_state_t {
    flag_view_t lap;
    flag_view_t green;
    flag_view_t red;
    flag_view_t yellow;
    flag_view_t blue;
    flag_view_t double_yellow;
    flag_view_t safety_car;
    flag_view_t vsc;
    lv_timer_t *flag_blink_timer = nullptr;
    lv_timer_t *double_yellow_blink_timer = nullptr;
    lv_timer_t *green_revert_timer = nullptr;
    uint32_t current_lap = 0;
    uint32_t total_laps = 0;
    uint32_t remaining_seconds = 0;
    flag_screen_t current_screen = FLAG_SCREEN_LAP;
};

static flag_display_state_t s_state;

static matrix_pattern_t GREEN_MATRIX_PATTERN = {MATRIX_PATTERN_SOLID, 'G', true, false};
static matrix_pattern_t RED_MATRIX_PATTERN = {MATRIX_PATTERN_SOLID, 'R', true, false};
static matrix_pattern_t YELLOW_MATRIX_PATTERN = {MATRIX_PATTERN_SOLID, 'Y', true, false};
static matrix_pattern_t BLUE_MATRIX_PATTERN = {MATRIX_PATTERN_SOLID, 'B', true, false};
static matrix_pattern_t DOUBLE_YELLOW_MATRIX_PATTERN = {MATRIX_PATTERN_DOUBLE_YELLOW, 'Y', true, false};
static matrix_pattern_t SAFETY_CAR_MATRIX_PATTERN = {MATRIX_PATTERN_SAFETY_CAR, '\0', true, false};
static matrix_pattern_t VSC_MATRIX_PATTERN = {MATRIX_PATTERN_VSC, '\0', true, false};

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

static matrix_pattern_t *get_blinking_flag_pattern(void) {
    switch (s_state.current_screen) {
        case FLAG_SCREEN_YELLOW:
            return &YELLOW_MATRIX_PATTERN;
        case FLAG_SCREEN_BLUE:
            return &BLUE_MATRIX_PATTERN;
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
    matrix_pattern_t *pattern = get_blinking_flag_pattern();
    if (pattern == nullptr) {
        return;
    }
    pattern->visible = !pattern->visible;
    lv_obj_invalidate(square);
}

static void set_lap_header(flag_view_t *view, uint32_t current_lap, uint32_t total_laps) {
    lv_label_set_text_fmt(view->lap_header, "LAP %u/%u", (unsigned)current_lap, (unsigned)total_laps);
}

static void set_time_header(flag_view_t *view, uint32_t remaining_seconds) {
    const uint32_t hours = remaining_seconds / 3600U;
    const uint32_t minutes = (remaining_seconds / 60U) % 60U;
    const uint32_t seconds = remaining_seconds % 60U;
    lv_label_set_text_fmt(view->time_header, "%02u:%02u:%02u", (unsigned)hours, (unsigned)minutes,
                          (unsigned)seconds);
}

static void update_all_headers(void) {
    flag_view_t *views[] = {&s_state.lap,          &s_state.green, &s_state.red, &s_state.yellow,
                            &s_state.blue,         &s_state.double_yellow, &s_state.safety_car, &s_state.vsc};
    for (flag_view_t *view : views) {
        set_lap_header(view, s_state.current_lap, s_state.total_laps);
        set_time_header(view, s_state.remaining_seconds);
    }
}

static void double_yellow_blink_timer_cb(lv_timer_t *timer) {
    (void)timer;
    if (s_state.double_yellow.square == nullptr) {
        return;
    }
    DOUBLE_YELLOW_MATRIX_PATTERN.triangle_state = !DOUBLE_YELLOW_MATRIX_PATTERN.triangle_state;
    lv_obj_invalidate(s_state.double_yellow.square);
}

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
    update_all_headers();
    pause_flag_timers();
    load_screen_locked(s_state.lap.screen, FLAG_SCREEN_LAP);
}

static void green_revert_timer_cb(lv_timer_t *timer) {
    (void)timer;
    show_lap_locked(s_state.current_lap, s_state.total_laps);
}

static void create_screen_base(flag_view_t *view) {
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
    lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *logo = lv_image_create(header);
    lv_image_set_src(logo, &F1_LOGO_IMAGE);
    lv_obj_align(logo, LV_ALIGN_TOP_MID, 0, 8);

    view->lap_header = lv_label_create(header);
    lv_obj_set_width(view->lap_header, BSP_DISPLAY_PANEL_WIDTH);
    lv_obj_set_style_text_color(view->lap_header, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->lap_header, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(view->lap_header, LV_TEXT_ALIGN_CENTER, 0);
    set_lap_header(view, s_state.current_lap, s_state.total_laps);
    lv_obj_update_layout(view->lap_header);
    const lv_coord_t lap_label_height = lv_obj_get_height(view->lap_header);
    ESP_LOGI(TAG, "lap_label_height: %d", lap_label_height);
    const lv_coord_t lap_label_top = LOGO_ROW_HEIGHT + (LAP_ROW_HEIGHT - lap_label_height) / 2;
    lv_obj_align(view->lap_header, LV_ALIGN_TOP_MID, 0, lap_label_top);

    view->time_header = lv_label_create(header);
    lv_obj_set_width(view->time_header, BSP_DISPLAY_PANEL_WIDTH);
    lv_obj_set_style_text_color(view->time_header, lv_color_white(), 0);
    lv_obj_set_style_text_font(view->time_header, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_align(view->time_header, LV_TEXT_ALIGN_CENTER, 0);
    set_time_header(view, s_state.remaining_seconds);
    const int32_t time_row_top = LOGO_ROW_HEIGHT + LAP_ROW_HEIGHT;
    lv_obj_update_layout(view->time_header);
    const lv_coord_t time_label_height = lv_obj_get_height(view->time_header);
    const lv_coord_t time_label_top = time_row_top + (HEADER_HEIGHT - time_row_top - time_label_height) / 2;
    lv_obj_align(view->time_header, LV_ALIGN_TOP_MID, 0, time_label_top - 8);
}

static void create_flag_screen(flag_view_t *view, matrix_pattern_t *pattern) {
    create_screen_base(view);

    view->square = lv_obj_create(view->screen);
    lv_obj_set_size(view->square, FLAG_SQUARE_SIZE, FLAG_SQUARE_SIZE);
    lv_obj_align(view->square, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(view->square, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(view->square, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(view->square, 0, 0);
    lv_obj_set_style_radius(view->square, 0, 0);
    lv_obj_add_event_cb(view->square, matrix_draw_event_cb, LV_EVENT_DRAW_MAIN, pattern);
}


esp_err_t flag_display_init(void) {
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    create_screen_base(&s_state.lap);

    create_flag_screen(&s_state.green, &GREEN_MATRIX_PATTERN);
    create_flag_screen(&s_state.red, &RED_MATRIX_PATTERN);
    create_flag_screen(&s_state.yellow, &YELLOW_MATRIX_PATTERN);
    create_flag_screen(&s_state.blue, &BLUE_MATRIX_PATTERN);
    create_flag_screen(&s_state.double_yellow, &DOUBLE_YELLOW_MATRIX_PATTERN);
    create_flag_screen(&s_state.safety_car, &SAFETY_CAR_MATRIX_PATTERN);
    create_flag_screen(&s_state.vsc, &VSC_MATRIX_PATTERN);

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
    if (s_state.lap.screen == nullptr || s_state.lap.lap_header == nullptr) {
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

esp_err_t flag_display_update_race_time(uint32_t remaining_seconds) {
    if (s_state.lap.screen == nullptr || s_state.lap.time_header == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    s_state.remaining_seconds = remaining_seconds;
    update_all_headers();

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

esp_err_t flag_display_show_red(void) {
    if (s_state.red.screen == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    pause_flag_timers();
    load_screen_locked(s_state.red.screen, FLAG_SCREEN_RED);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_yellow(const char *turn_info) {
    (void)turn_info;
    if (s_state.yellow.screen == nullptr || s_state.yellow.square == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    YELLOW_MATRIX_PATTERN.visible = true;
    lv_obj_invalidate(s_state.yellow.square);
    pause_flag_timers();
    lv_timer_reset(s_state.flag_blink_timer);
    lv_timer_resume(s_state.flag_blink_timer);
    load_screen_locked(s_state.yellow.screen, FLAG_SCREEN_YELLOW);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_blue(const char *car_number) {
    (void)car_number;
    if (s_state.blue.screen == nullptr || s_state.blue.square == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    BLUE_MATRIX_PATTERN.visible = true;
    lv_obj_invalidate(s_state.blue.square);
    pause_flag_timers();
    lv_timer_reset(s_state.flag_blink_timer);
    lv_timer_resume(s_state.flag_blink_timer);
    load_screen_locked(s_state.blue.screen, FLAG_SCREEN_BLUE);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_double_yellow(const char *turn_info) {
    (void)turn_info;
    if (s_state.double_yellow.screen == nullptr || s_state.double_yellow.square == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    pause_flag_timers();
    DOUBLE_YELLOW_MATRIX_PATTERN.triangle_state = false;
    lv_timer_reset(s_state.double_yellow_blink_timer);
    lv_timer_resume(s_state.double_yellow_blink_timer);
    load_screen_locked(s_state.double_yellow.screen, FLAG_SCREEN_DOUBLE_YELLOW);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_safety_car(void) {
    if (s_state.safety_car.screen == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    pause_flag_timers();
    load_screen_locked(s_state.safety_car.screen, FLAG_SCREEN_SAFETY_CAR);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_show_vsc(void) {
    if (s_state.vsc.screen == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    pause_flag_timers();
    load_screen_locked(s_state.vsc.screen, FLAG_SCREEN_VSC);

    lvgl_port_unlock();
    return ESP_OK;
}
