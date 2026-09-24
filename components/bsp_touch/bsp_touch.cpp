#include "bsp_touch.h"

#include "esp_log.h"

static const char *TAG = "bsp_touch";

esp_err_t bsp_touch_init(void) {
    ESP_LOGW(TAG, "Touch controller init is staged pending confirmation of the actual touch IC and I2C address from the board schematic.");
    return ESP_OK;
}

esp_err_t bsp_touch_deinit(void) {
    ESP_LOGI(TAG, "Touch deinit placeholder called.");
    return ESP_OK;
}

esp_err_t bsp_touch_read(bsp_touch_point_t *point) {
    if (point == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    point->x = 0;
    point->y = 0;
    point->pressed = false;
    ESP_LOGI(TAG, "Touch read placeholder: no hardware touch IC validated yet.");
    return ESP_OK;
}

esp_err_t bsp_touch_reset(void) {
    ESP_LOGI(TAG, "Touch reset placeholder called.");
    return ESP_OK;
}
