#include "esp_log.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_pmic.h"
#include "bsp_touch.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_box_printer.h"

#include <string>

static const char *TAG = "main";

static void touch_color_task(void *arg)
{
    (void)arg;
    constexpr uint32_t test_colors[] = {
        0xFF0000,
        0x00FF00,
        0x0000FF,
        0xFFFFFF,
        0x000000,
    };
    constexpr const char *color_names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
    constexpr size_t color_count = sizeof(test_colors) / sizeof(test_colors[0]);
    size_t color_index = color_count - 1U;
    bool previous_pressed = false;

    while (true) {
        bsp_touch_point_t point = {};
        esp_err_t err = bsp_touch_read(&point);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Touch read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (point.pressed && !previous_pressed) {
            color_index = (color_index + 1U) % color_count;
            err = bsp_display_set_solid_color(test_colors[color_index]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Touch color update failed: %s", esp_err_to_name(err));
            } else {
                app::SerialBoxPrinter printer("TOUCH COLOR");
                printer.add_body_bullet("Position: " + std::to_string(point.x) + ", " + std::to_string(point.y), 2U);
                printer.add_body_bullet("Screen color: " + std::string(color_names[color_index]), 2U);
                printer.print();
            }
        }
        previous_pressed = point.pressed;
        vTaskDelay(pdMS_TO_TICKS(30));
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

    err = bsp_display_set_solid_color(0x00C853);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initial display color failed: %s", esp_err_to_name(err));
        return;
    }

    err = bsp_touch_init(BSP_DISPLAY_PANEL_WIDTH, BSP_DISPLAY_PANEL_HEIGHT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch initialization failed: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t task_result = xTaskCreate(touch_color_task, "touch_color", 4096, nullptr, 3, nullptr);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create touch color task");
        return;
    }
}
