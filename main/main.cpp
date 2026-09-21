#include <cstdio>

#include "esp_log.h"
#include "bsp_board.h"

static const char *TAG = "main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting phase-1 BSP validation");

    esp_err_t err = bsp_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_board_init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Board GPIO layer initialized");

    err = bsp_board_i2c_scan();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2C scan complete");
    } else {
        ESP_LOGW(TAG, "I2C scan did not detect devices; board power and schematic validation are still required");
    }

    ESP_LOGI(TAG, "Phase-1 validation ready for schematic-confirmed PMIC and GPIO checks");
}
