#include "esp_log.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_pmic.h"
#include "bsp_touch.h"
#include "flag_display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_box_printer.h"

#include <string>

static const char *TAG = "main";

static void flag_stage_task(void *arg)
{
    (void)arg;
    constexpr flag_screen_t stages[] = {FLAG_SCREEN_LAP, FLAG_SCREEN_GREEN, FLAG_SCREEN_YELLOW, FLAG_SCREEN_DOUBLE_YELLOW};
    constexpr const char *stage_names[] = {"LAP", "GREEN FLAG", "YELLOW FLAG", "DOUBLE YELLOW FLAG"};
    constexpr size_t stage_count = sizeof(stages) / sizeof(stages[0]);
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
            // Look up the live stage (it may have changed internally, e.g. green auto-revert) before advancing.
            flag_screen_t current = flag_display_get_current_screen();
            size_t current_index = 0;
            for (size_t i = 0; i < stage_count; ++i) {
                if (stages[i] == current) {
                    current_index = i;
                    break;
                }
            }
            size_t stage_index = (current_index + 1U) % stage_count;
            switch (stages[stage_index]) {
                case FLAG_SCREEN_LAP:
                    err = flag_display_show_lap(15, 52);
                    break;
                case FLAG_SCREEN_GREEN:
                    err = flag_display_show_green();
                    break;
                case FLAG_SCREEN_YELLOW:
                    err = flag_display_show_yellow("TURN 6");
                    break;
                case FLAG_SCREEN_DOUBLE_YELLOW:
                    err = flag_display_show_double_yellow("TURN 6");
                    break;
                default:
                    err = ESP_ERR_NOT_SUPPORTED;
                    break;
            }
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Stage change failed: %s", esp_err_to_name(err));
            } else {
                app::SerialBoxPrinter printer("FLAG STAGE");
                printer.add_body_bullet("Position: " + std::to_string(point.x) + ", " + std::to_string(point.y), 2U);
                printer.add_body_bullet("Stage: " + std::string(stage_names[stage_index]), 2U);
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

    err = flag_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flag display init failed: %s", esp_err_to_name(err));
        return;
    }

    err = flag_display_show_lap(15, 52);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flag display show_lap failed: %s", esp_err_to_name(err));
        return;
    }

    err = bsp_touch_init(BSP_DISPLAY_PANEL_WIDTH, BSP_DISPLAY_PANEL_HEIGHT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Touch initialization failed: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t task_result = xTaskCreate(flag_stage_task, "flag_stage", 4096, nullptr, 3, nullptr);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create flag stage task");
        return;
    }
}
