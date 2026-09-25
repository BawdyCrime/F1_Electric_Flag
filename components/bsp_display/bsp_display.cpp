#include "bsp_display.h"

#include "esp_log.h"
#include "driver/spi_master.h"

#include "bsp_board.h"

static const char *TAG = "bsp_display";

esp_err_t bsp_display_init(void) {
    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num = BSP_SPI_SCLK_GPIO;
    bus_cfg.data0_io_num = BSP_SPI_QSPI_IO0_GPIO;
    bus_cfg.data1_io_num = BSP_SPI_QSPI_IO1_GPIO;
    bus_cfg.data2_io_num = BSP_SPI_QSPI_IO2_GPIO;
    bus_cfg.data3_io_num = BSP_SPI_QSPI_IO3_GPIO;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t err = spi_bus_initialize(BSP_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "LCD SPI bus initialized on host=%d, SCLK=%d, D0..D3=%d/%d/%d/%d, CS=%d",
             BSP_SPI_HOST,
             BSP_SPI_SCLK_GPIO,
             BSP_SPI_QSPI_IO0_GPIO,
             BSP_SPI_QSPI_IO1_GPIO,
             BSP_SPI_QSPI_IO2_GPIO,
             BSP_SPI_QSPI_IO3_GPIO,
             BSP_SPI_CS_GPIO);
    return ESP_OK;
}

esp_err_t bsp_display_deinit(void) {
    esp_err_t err = spi_bus_free(BSP_SPI_HOST);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD SPI bus free failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "Display SPI bus deinitialized.");
    return ESP_OK;
}

esp_err_t bsp_display_set_backlight(bool enabled) {
    ESP_LOGI(TAG, "Display backlight %s", enabled ? "enabled" : "disabled");
    return bsp_board_set_backlight(enabled);
}

esp_err_t bsp_display_set_rotation(uint16_t rotation) {
    (void)rotation;
    ESP_LOGI(TAG, "Display rotation set to %u (panel driver not yet initialized)", static_cast<unsigned>(rotation));
    return ESP_OK;
}
