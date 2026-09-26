#include <cstdio>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_pmic.h"
static const char *TAG = "main";

static constexpr struct {
    const char *name;
    uint32_t background;
    uint32_t foreground;
} kDisplayTestColors[] = {
    {"red", 0xFF0000, 0xFFFFFF},
    {"green", 0x00FF00, 0x000000},
    {"blue", 0x0000FF, 0xFFFFFF},
    {"white", 0xFFFFFF, 0x000000},
    {"black", 0x000000, 0xFFFFFF},
    {"yellow", 0xFFFF00, 0x000000},
};

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

    err = bsp_display_lvgl_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL display init failed: %s", esp_err_to_name(err));
        return;
    }

    if (!lvgl_port_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock LVGL");
        return;
    }
    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "F1 Electric Flag\nLVGL display ready");
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    lvgl_port_unlock();

    for (;;) {
        for (const auto &color : kDisplayTestColors) {
            ESP_LOGI(TAG, "Display test color: %s", color.name);
            if (!lvgl_port_lock(0)) {
                ESP_LOGE(TAG, "Failed to lock LVGL");
                return;
            }
            lv_obj_set_style_bg_color(screen, lv_color_hex(color.background), 0);
            lv_obj_set_style_text_color(label, lv_color_hex(color.foreground), 0);
            char label_text[64];
            std::snprintf(label_text, sizeof(label_text), "F1 Electric Flag\nLVGL RGB565: %s", color.name);
            lv_label_set_text(label, label_text);
            lv_refr_now(lv_display_get_default());
            lvgl_port_unlock();
            vTaskDelay(pdMS_TO_TICKS(700));
        }
    }
}
