#include "esp_log.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_pmic.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "main";

static void display_color_cycle_task(void *arg)
{
    (void)arg;
    constexpr uint32_t test_colors[] = {
        0xFF0000,
        0x00FF00,
        0x0000FF,
        0xFFFFFF,
        0x000000,
    };
    size_t color_index = 0;

    while (true) {
        const uint32_t color = test_colors[color_index];
        esp_err_t err = bsp_display_set_solid_color(color);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LVGL color update failed: %s", esp_err_to_name(err));
            vTaskDelete(nullptr);
            return;
        }
        ESP_LOGI(TAG, "Display test color: #%06lX", static_cast<unsigned long>(color));
        color_index = (color_index + 1U) % (sizeof(test_colors) / sizeof(test_colors[0]));
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

extern "C" void app_main(void)
{
    esp_err_t err = bsp_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_board_init failed: %s", esp_err_to_name(err));
        return;
    }

    bsp_board_print_info();
    bsp_board_i2c_scan();

    bsp_board_print_peripheral();

    err = bsp_pmic_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PMIC validation failed: %s", esp_err_to_name(err));
        return;
    }

    bsp_pmic_print_status();

    err = bsp_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t task_result = xTaskCreate(display_color_cycle_task, "display_color_test", 3072, nullptr, 3, nullptr);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display color test task");
        return;
    }
}
