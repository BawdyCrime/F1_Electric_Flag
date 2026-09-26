#include "bsp_display.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"

#include "bsp_board.h"

static const char *TAG = "bsp_display";
static esp_lcd_panel_io_handle_t s_panel_io = nullptr;
static esp_lcd_panel_handle_t s_panel = nullptr;
static bool s_spi_bus_initialized = false;

esp_err_t bsp_display_init(void) {
    if (s_panel != nullptr) {
        return ESP_OK;
    }

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
    s_spi_bus_initialized = true;

    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.cs_gpio_num = BSP_SPI_CS_GPIO;
    io_config.dc_gpio_num = GPIO_NUM_NC;
    io_config.spi_mode = 3;
    io_config.pclk_hz = BSP_SPI_CLOCK_HZ;
    io_config.trans_queue_depth = 10;
    io_config.lcd_cmd_bits = 32;
    io_config.lcd_param_bits = 8;
    io_config.flags.quad_mode = true;
    err = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)BSP_SPI_HOST, &io_config, &s_panel_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel IO init failed: %s", esp_err_to_name(err));
        spi_bus_free(BSP_SPI_HOST);
        s_spi_bus_initialized = false;
        return err;
    }

    axs15231b_vendor_config_t vendor_config = {};
    vendor_config.flags.use_qspi_interface = 1;

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = GPIO_NUM_NC;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor_config;
    err = esp_lcd_new_panel_axs15231b(s_panel_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel create failed: %s", esp_err_to_name(err));
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = nullptr;
        spi_bus_free(BSP_SPI_HOST);
        s_spi_bus_initialized = false;
        return err;
    }

    err = esp_lcd_panel_reset(s_panel);
    if (err == ESP_OK) {
        err = esp_lcd_panel_init(s_panel);
    }
    if (err == ESP_OK) {
        err = esp_lcd_panel_disp_on_off(s_panel, true);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD panel startup failed: %s", esp_err_to_name(err));
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = nullptr;
        spi_bus_free(BSP_SPI_HOST);
        s_spi_bus_initialized = false;
        return err;
    }

    err = bsp_display_set_backlight(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD backlight enable failed: %s", esp_err_to_name(err));
        esp_lcd_panel_disp_on_off(s_panel, false);
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = nullptr;
        spi_bus_free(BSP_SPI_HOST);
        s_spi_bus_initialized = false;
        return err;
    }

    ESP_LOGI(TAG, "AXS15231B display initialized and backlight enabled on host=%d, SCLK=%d, D0..D3=%d/%d/%d/%d, CS=%d",
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
    esp_err_t first_err = bsp_display_set_backlight(false);
    if (s_panel != nullptr) {
        esp_err_t err = esp_lcd_panel_del(s_panel);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
        s_panel = nullptr;
    }
    if (s_panel_io != nullptr) {
        esp_err_t err = esp_lcd_panel_io_del(s_panel_io);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
        s_panel_io = nullptr;
    }
    if (s_spi_bus_initialized) {
        esp_err_t err = spi_bus_free(BSP_SPI_HOST);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
        s_spi_bus_initialized = false;
    }
    if (first_err != ESP_OK) {
        ESP_LOGE(TAG, "Display deinit failed: %s", esp_err_to_name(first_err));
        return first_err;
    }
    ESP_LOGI(TAG, "Display deinitialized.");
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
