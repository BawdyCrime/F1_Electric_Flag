#include "bsp_imu.h"

#include "esp_log.h"

static const char *TAG = "bsp_imu";

esp_err_t bsp_imu_init(void) {
    ESP_LOGW(TAG, "IMU init is staged pending chip identification and register map validation from the schematic.");
    return ESP_OK;
}

esp_err_t bsp_imu_deinit(void) {
    ESP_LOGI(TAG, "IMU deinit placeholder called.");
    return ESP_OK;
}

esp_err_t bsp_imu_read(bsp_imu_data_t *data) {
    if (data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    data->ax = 0.0f;
    data->ay = 0.0f;
    data->az = 0.0f;
    data->gx = 0.0f;
    data->gy = 0.0f;
    data->gz = 0.0f;
    ESP_LOGI(TAG, "IMU read placeholder: sensor not yet validated on hardware.");
    return ESP_OK;
}
