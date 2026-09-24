#include "bsp_display.h"

#include "esp_log.h"

static const char *TAG = "bsp_display";

esp_err_t bsp_display_init(void) {
    ESP_LOGW(TAG, "Display init is staged pending schematic validation of panel interface, reset, and power pins.");
    return ESP_OK;
}

esp_err_t bsp_display_deinit(void) {
    ESP_LOGI(TAG, "Display deinit placeholder called.");
    return ESP_OK;
}

esp_err_t bsp_display_set_backlight(bool enabled) {
    ESP_LOGI(TAG, "Display backlight %s (placeholder, hardware pins must be confirmed from schematic)", enabled ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t bsp_display_set_rotation(uint16_t rotation) {
    (void)rotation;
    ESP_LOGI(TAG, "Display rotation placeholder applied.");
    return ESP_OK;
}
