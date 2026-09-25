#include <cstdio>
#include <string>
#include <vector>

#include "esp_log.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_pmic.h"

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
    bsp_board_print_info();

    err = bsp_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Display SPI init failed: %s", esp_err_to_name(err));
        return;
    }

    err = bsp_board_i2c_scan();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2C scan complete");
    } else {
        ESP_LOGW(TAG, "I2C scan did not detect devices; board power and schematic validation are still required");
    }

    bsp_board_print_peripheral_summary();

    err = bsp_pmic_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PMIC validation failed: %s", esp_err_to_name(err));
        return;
    }

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t chip_id = 0;
    err = bsp_pmic_read_status(&status1, &status2, &chip_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PMIC status: chip_id=0x%02X, status1=0x%02X, status2=0x%02X", chip_id, status1, status2);
    } else {
        ESP_LOGE(TAG, "PMIC status readback failed: %s", esp_err_to_name(err));
    }

    bsp_pmic_print_status();

    ESP_LOGI(TAG, "Phase-1 validation ready for schematic-confirmed PMIC rail sequencing and GPIO checks");
}
