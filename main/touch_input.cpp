#include "touch_input.h"

#include "bsp_touch.h"
#include "session_selector.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch_input";

static void touch_task(void *)
{
    while (true) {
        bsp_touch_point_t point = {};
        esp_err_t err = bsp_touch_read(&point);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Touch read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        session_selector_handle_touch(point.pressed, point.x, point.y);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

esp_err_t touch_input_start()
{
    return xTaskCreate(touch_task, "touch_task", 4096, nullptr, 3, nullptr) == pdPASS ? ESP_OK : ESP_FAIL;
}
