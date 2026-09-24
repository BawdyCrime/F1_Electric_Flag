#include "bsp_pmic.h"

#include "esp_log.h"

#include "driver/i2c_master.h"
#include "bsp_board.h"

static const char *TAG = "bsp_pmic";

static i2c_master_dev_handle_t s_pmic_dev_handle = NULL;
static bool s_pmic_initialized = false;

static esp_err_t bsp_pmic_read_reg(uint8_t reg, uint8_t *value) {
    if (s_pmic_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t write_buf[] = { reg };
    uint8_t read_buf[] = { 0x00 };
    esp_err_t err = i2c_master_transmit_receive(s_pmic_dev_handle, write_buf, sizeof(write_buf),
                                                read_buf, sizeof(read_buf), 1000);
    if (err != ESP_OK) {
        return err;
    }

    *value = read_buf[0];
    return ESP_OK;
}

esp_err_t bsp_pmic_init(void) {
    if (s_pmic_initialized) {
        return ESP_OK;
    }

    if (bsp_board_get_i2c_bus_handle() == NULL) {
        esp_err_t err = bsp_board_init();
        if (err != ESP_OK) {
            return err;
        }
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = BSP_PMIC_I2C_ADDR,
        .scl_speed_hz = 100000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };

    esp_err_t err = i2c_master_bus_add_device(bsp_board_get_i2c_bus_handle(), &dev_cfg, &s_pmic_dev_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add AXP2101 device on I2C: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t chip_id = 0;
    err = bsp_pmic_read_status(&status1, &status2, &chip_id);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PMIC readback failed: %s", esp_err_to_name(err));
        return err;
    }

    if (chip_id != 0x4A) {
        ESP_LOGW(TAG, "AXP2101 chip ID mismatch: expected 0x4A, got 0x%02X", chip_id);
    }

    ESP_LOGI(TAG, "PMIC validated: chip_id=0x%02X status1=0x%02X status2=0x%02X", chip_id, status1, status2);
    s_pmic_initialized = true;
    return ESP_OK;
}

esp_err_t bsp_pmic_deinit(void) {
    if (s_pmic_dev_handle != NULL) {
        i2c_master_bus_rm_device(s_pmic_dev_handle);
        s_pmic_dev_handle = NULL;
    }
    s_pmic_initialized = false;
    ESP_LOGI(TAG, "PMIC deinit complete");
    return ESP_OK;
}

esp_err_t bsp_pmic_read_status(uint8_t *status1, uint8_t *status2, uint8_t *chip_id) {
    if (status1 == NULL || status2 == NULL || chip_id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_pmic_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = bsp_pmic_read_reg(BSP_PMIC_STATUS1_REG, status1);
    if (err != ESP_OK) {
        return err;
    }

    err = bsp_pmic_read_reg(BSP_PMIC_STATUS2_REG, status2);
    if (err != ESP_OK) {
        return err;
    }

    return bsp_pmic_read_reg(BSP_PMIC_CHIP_ID_REG, chip_id);
}

esp_err_t bsp_pmic_enable_rails(bool enable) {
    (void)enable;
    ESP_LOGW(TAG, "PMIC rail enable is intentionally left disabled until the board sequencing is confirmed from the schematic.");
    return ESP_OK;
}

esp_err_t bsp_pmic_set_backlight_enable(bool enable) {
    (void)enable;
    ESP_LOGI(TAG, "Backlight enable remains controlled by the PMIC rail sequence; no direct rail toggle is performed yet.");
    return ESP_OK;
}
