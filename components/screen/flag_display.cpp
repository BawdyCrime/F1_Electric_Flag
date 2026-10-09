#include "flag_display.h"

#include <cstdio>
#include <strings.h>

#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "event_timing.h"
#include "flag_panel.h"
#include "lvgl.h"
#include "matrix_pattern.h"
#include "screen_header.h"

#include <cstdint>

static const char *TAG = "flag_display";

static constexpr uint32_t BLINK_PERIOD_MS = 500;
static constexpr uint32_t GREEN_AUTO_REVERT_MS = 5 * 1000; // 10 seconds

struct flag_display_state_t {
    flag_view_t event_timing;
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
    int32_t remaining_seconds = 0;
    char session_name[32] = "";
    char title[48] = "";
    flag_screen_t current_screen = FLAG_SCREEN_EVENT_TIMING;
};

static flag_display_state_t s_state;

// Header title: "RACE - LAP x/y" for races, otherwise the session name.
static void refresh_title(void) {
    if (strcasecmp(s_state.session_name, "Race") == 0) {
        if (s_state.current_lap == 0) {
            snprintf(s_state.title, sizeof(s_state.title), "RACE");
        } else if (s_state.total_laps == 0) {
            snprintf(s_state.title, sizeof(s_state.title), "RACE - LAP %u", (unsigned)s_state.current_lap);
        } else {
            snprintf(s_state.title, sizeof(s_state.title), "RACE - LAP %u/%u", (unsigned)s_state.current_lap,
                     (unsigned)s_state.total_laps);
        }
    } else {
        snprintf(s_state.title, sizeof(s_state.title), "%s", s_state.session_name);
    }
}

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
        case FLAG_SCREEN_SAFETY_CAR:
            return s_state.safety_car.square;
        case FLAG_SCREEN_VSC:
            return s_state.vsc.square;
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
        case FLAG_SCREEN_SAFETY_CAR:
            return &SAFETY_CAR_MATRIX_PATTERN;
        case FLAG_SCREEN_VSC:
            return &VSC_MATRIX_PATTERN;
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

static void update_all_headers(void) {
    flag_view_t *views[] = {&s_state.event_timing, &s_state.green, &s_state.red, &s_state.yellow,
                            &s_state.blue,         &s_state.double_yellow, &s_state.safety_car, &s_state.vsc};
    for (flag_view_t *view : views) {
        screen_header_update(view, s_state.title, s_state.remaining_seconds);
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

static void pause_flag_timers(void) {
    lv_timer_pause(s_state.flag_blink_timer);
    lv_timer_pause(s_state.double_yellow_blink_timer);
    lv_timer_pause(s_state.green_revert_timer);
}

static void load_screen_locked(lv_obj_t *screen, flag_screen_t screen_id) {
    lv_scr_load(screen);
    s_state.current_screen = screen_id;
}

static esp_err_t show_screen(flag_view_t *view, flag_screen_t screen_id) {
    if (view == nullptr || view->screen == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    pause_flag_timers();
    load_screen_locked(view->screen, screen_id);
    lvgl_port_unlock();
    return ESP_OK;
}

static esp_err_t show_blinking_matrix_flag(flag_view_t *view, flag_screen_t screen_id,
                                           matrix_pattern_t *pattern) {
    if (view == nullptr || view->screen == nullptr || view->square == nullptr || pattern == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    pause_flag_timers();
    pattern->visible = true;
    lv_obj_invalidate(view->square);
    lv_timer_reset(s_state.flag_blink_timer);
    lv_timer_resume(s_state.flag_blink_timer);
    load_screen_locked(view->screen, screen_id);
    lvgl_port_unlock();
    return ESP_OK;
}

// Updates timing content and shared headers; assumes the LVGL port lock is already held.
static void update_event_timing_locked(const char *session_name, uint32_t current_lap, uint32_t total_laps,
                                       const event_timing_row_t *rows, size_t row_count) {
    event_timing_update(&s_state.event_timing, rows, row_count);
    s_state.current_lap = current_lap;
    s_state.total_laps = total_laps;
    snprintf(s_state.session_name, sizeof(s_state.session_name), "%s", session_name != nullptr ? session_name : "");
    refresh_title();
    update_all_headers();
}

// Shows the event timing screen; assumes the LVGL port lock is already held.
static void show_event_timing_locked(const char *session_name, uint32_t current_lap, uint32_t total_laps,
                                    const event_timing_row_t *rows, size_t row_count) {
    update_event_timing_locked(session_name, current_lap, total_laps, rows, row_count);
    pause_flag_timers();
    load_screen_locked(s_state.event_timing.screen, FLAG_SCREEN_EVENT_TIMING);
}

static void green_revert_timer_cb(lv_timer_t *timer) {
    (void)timer;
    pause_flag_timers();
    load_screen_locked(s_state.event_timing.screen, FLAG_SCREEN_EVENT_TIMING);
}

esp_err_t flag_display_init(void) {
    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    screen_header_create(&s_state.event_timing, s_state.title, s_state.remaining_seconds);
    event_timing_create(&s_state.event_timing);

    flag_view_t *flag_views[] = {&s_state.green, &s_state.red, &s_state.yellow, &s_state.blue,
                                 &s_state.double_yellow, &s_state.safety_car, &s_state.vsc};
    matrix_pattern_t *flag_patterns[] = {&GREEN_MATRIX_PATTERN, &RED_MATRIX_PATTERN, &YELLOW_MATRIX_PATTERN,
                                         &BLUE_MATRIX_PATTERN, &DOUBLE_YELLOW_MATRIX_PATTERN,
                                         &SAFETY_CAR_MATRIX_PATTERN, &VSC_MATRIX_PATTERN};
    for (size_t i = 0; i < sizeof(flag_views) / sizeof(flag_views[0]); ++i) {
        screen_header_create(flag_views[i], s_state.title, s_state.remaining_seconds);
        flag_panel_create(flag_views[i], flag_patterns[i]);
    }

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

esp_err_t flag_display_show_event_timing(const char *session_name, uint32_t current_lap, uint32_t total_laps,
                                         const event_timing_row_t *rows, size_t row_count) {
    if (s_state.event_timing.screen == nullptr || s_state.event_timing.lap_header == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    show_event_timing_locked(session_name, current_lap, total_laps, rows, row_count);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_update_event_timing(const char *session_name, uint32_t current_lap, uint32_t total_laps,
                                           const event_timing_row_t *rows, size_t row_count) {
    if (s_state.event_timing.screen == nullptr || s_state.event_timing.lap_header == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "lvgl_port_lock failed");
        return ESP_FAIL;
    }

    update_event_timing_locked(session_name, current_lap, total_laps, rows, row_count);

    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t flag_display_update_race_time(int32_t remaining_seconds) {
    if (s_state.event_timing.screen == nullptr || s_state.event_timing.time_header == nullptr) {
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
    return show_screen(&s_state.red, FLAG_SCREEN_RED);
}

esp_err_t flag_display_show_yellow(void) {
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

esp_err_t flag_display_show_blue(void) {
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

esp_err_t flag_display_show_double_yellow(void) {
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
    return show_blinking_matrix_flag(&s_state.safety_car, FLAG_SCREEN_SAFETY_CAR, &SAFETY_CAR_MATRIX_PATTERN);
}

esp_err_t flag_display_show_vsc(void) {
    return show_blinking_matrix_flag(&s_state.vsc, FLAG_SCREEN_VSC, &VSC_MATRIX_PATTERN);
}
