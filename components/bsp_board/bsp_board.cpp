#include "bsp_board.h"

#include <stdio.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_board";
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

static esp_err_t bsp_board_configure_gpio(gpio_num_t gpio, bool output, bool pullup, bool pulldown) {
    if (gpio == GPIO_NUM_NC) {
        return ESP_OK;
    }

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << gpio);
    io_conf.mode = output ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
    io_conf.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&io_conf);
}

esp_err_t bsp_board_init(void) {
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "Initializing board-level GPIO and I2C validation layer");

    err = bsp_board_configure_gpio(BSP_TOUCH_INT_GPIO, false, true, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch interrupt GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_board_configure_gpio(BSP_TOUCH_RST_GPIO, true, false, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch reset GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }
    gpio_set_level(BSP_TOUCH_RST_GPIO, 1);

    err = bsp_board_configure_gpio(BSP_STATUS_LED_GPIO, true, false, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "status LED GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = BSP_I2C_PORT,
        .sda_io_num = BSP_I2C_SDA_GPIO,
        .scl_io_num = BSP_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = true,
            .allow_pd = false,
        },
    };

    err = i2c_new_master_bus(&bus_cfg, &s_i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "I2C bus initialized on SDA=%d, SCL=%d", BSP_I2C_SDA_GPIO, BSP_I2C_SCL_GPIO);
    return ESP_OK;
}

esp_err_t bsp_board_i2c_scan(void) {
    bool found_any = false;
    const uint8_t pmic_candidates[] = {0x34, 0x35};
    const uint8_t imu_candidates[] = {0x6A, 0x6B};

    if (s_i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus is not initialized; call bsp_board_init() first.");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Scanning I2C bus for board peripherals");

    for (uint8_t addr = 0; addr < 0x80; ++addr) {
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, addr, 1000);
        if (err == ESP_OK) {
            found_any = true;
            ESP_LOGI(TAG, "Detected I2C device at 0x%02X", addr);
        }
    }

    if (!found_any) {
        ESP_LOGW(TAG, "No I2C devices detected. Check board power, pull-ups, and wiring.");
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < sizeof(pmic_candidates); ++i) {
        if (i2c_master_probe(s_i2c_bus_handle, pmic_candidates[i], 1000) == ESP_OK) {
            ESP_LOGI(TAG, "PMIC candidate detected at 0x%02X", pmic_candidates[i]);
        }
    }

    for (size_t i = 0; i < sizeof(imu_candidates); ++i) {
        if (i2c_master_probe(s_i2c_bus_handle, imu_candidates[i], 1000) == ESP_OK) {
            ESP_LOGI(TAG, "IMU candidate detected at 0x%02X", imu_candidates[i]);
        }
    }

    return ESP_OK;
}

esp_err_t bsp_board_set_backlight(bool enabled) {
    if (BSP_LCD_BL_GPIO == GPIO_NUM_NC) {
        ESP_LOGW(TAG, "Backlight GPIO not configured; board pin map must be confirmed from schematic before enabling panel power.");
        return ESP_OK;
    }

    esp_err_t err = bsp_board_configure_gpio(BSP_LCD_BL_GPIO, true, false, false);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(BSP_LCD_BL_GPIO, enabled ? 1 : 0);
    ESP_LOGI(TAG, "Backlight %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t bsp_board_set_status_led(bool enabled) {
    if (BSP_STATUS_LED_GPIO == GPIO_NUM_NC) {
        return ESP_OK;
    }

    esp_err_t err = bsp_board_configure_gpio(BSP_STATUS_LED_GPIO, true, false, false);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(BSP_STATUS_LED_GPIO, enabled ? 1 : 0);
    ESP_LOGI(TAG, "Status LED %s", enabled ? "enabled" : "disabled");
    return ESP_OK;
}
