#include "touch_input.h"

#include "bsp_display.h"
#include "bsp_touch.h"
#include "flag_display.h"
#include "screen_header.h"
#include "session_selector.h"

#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch_input";
static constexpr int OFFSET_STEP_S = 10;

static void touch_task(void *)
{
    bool was_pressed = false;
    while (true) {
        bsp_touch_point_t point = {};
        esp_err_t err = bsp_touch_read(&point);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Touch read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        session_selector_handle_touch(point.pressed, point.x, point.y);
        const bool on_logo = screen_header_hit_logo(point.x, point.y);
        if (point.pressed && !was_pressed && on_logo) {
            esp_restart(); // hidden feature: touching the logo on any page restarts the board
        }
        if (point.pressed && !was_pressed && !on_logo && screen_header_hit_header(point.x, point.y)) {
            replay_adjust_offset(point.x < BSP_DISPLAY_PANEL_WIDTH / 2 ? -OFFSET_STEP_S : OFFSET_STEP_S);
        }
        was_pressed = point.pressed;
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

esp_err_t touch_input_start()
{
    return xTaskCreate(touch_task, "touch_task", 4096, nullptr, 3, nullptr) == pdPASS ? ESP_OK : ESP_FAIL;
}
