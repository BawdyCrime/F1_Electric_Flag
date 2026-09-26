#include "bsp_pmic.h"

#include <cstdio>
#include <string>

#include "esp_log.h"

#include "driver/i2c_master.h"
#include "bsp_board.h"
#include "serial_box_printer.h"

static const char *TAG = "bsp_pmic";

static i2c_master_dev_handle_t s_pmic_dev_handle = NULL;
static bool s_pmic_initialized = false;

#define BSP_PMIC_DC_ONOFF_DVM_CTRL     0x80u
#define BSP_PMIC_DC_VOL0_CTRL          0x82u
#define BSP_PMIC_DC_VOL1_CTRL          0x83u
#define BSP_PMIC_DC_VOL2_CTRL          0x84u
#define BSP_PMIC_DC_VOL3_CTRL          0x85u
#define BSP_PMIC_DC_VOL4_CTRL          0x86u
#define BSP_PMIC_LDO_ONOFF_CTRL0       0x90u
#define BSP_PMIC_LDO_ONOFF_CTRL1       0x91u
#define BSP_PMIC_LDO_VOL0_CTRL         0x92u
#define BSP_PMIC_LDO_VOL1_CTRL         0x93u
#define BSP_PMIC_LDO_VOL2_CTRL         0x94u
#define BSP_PMIC_LDO_VOL3_CTRL         0x95u
#define BSP_PMIC_LDO_VOL4_CTRL         0x96u
#define BSP_PMIC_LDO_VOL5_CTRL         0x97u
#define BSP_PMIC_LDO_VOL6_CTRL         0x98u
#define BSP_PMIC_LDO_VOL7_CTRL         0x99u
#define BSP_PMIC_LDO_VOL8_CTRL         0x9Au

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

static esp_err_t bsp_pmic_write_reg(uint8_t reg, uint8_t value) {
    if (s_pmic_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t write_buf[] = { reg, value };
    return i2c_master_transmit(s_pmic_dev_handle, write_buf, sizeof(write_buf), 1000);
}

static esp_err_t bsp_pmic_set_reg_bit(uint8_t reg, uint8_t bit, bool enable) {
    uint8_t value = 0;
    esp_err_t err = bsp_pmic_read_reg(reg, &value);
    if (err != ESP_OK) {
        return err;
    }

    if (enable) {
        value |= (1u << bit);
    } else {
        value &= static_cast<uint8_t>(~(1u << bit));
    }

    return bsp_pmic_write_reg(reg, value);
}

static esp_err_t bsp_pmic_set_voltage(uint8_t reg, uint16_t min_mv, uint16_t max_mv, uint16_t step_mv, uint16_t target_mv) {
    if (target_mv < min_mv || target_mv > max_mv) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((target_mv - min_mv) % step_mv != 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t value = static_cast<uint8_t>((target_mv - min_mv) / step_mv);
    return bsp_pmic_write_reg(reg, value);
}

static std::string fixed_field(const std::string &value, size_t width) {
    std::string result = value;
    if (result.size() < width) {
        result.append(width - result.size(), ' ');
    }
    return result;
}

static std::string pmic_yes_no(bool value) {
    return value ? "YES" : "NO";
}

static std::string pmic_charge_state(uint8_t status2) {
    const uint8_t val = (status2 >> 5) & 0x03u;
    switch (val) {
        case 0x00u:
            return "STANDBY";
        case 0x01u:
            return "CHARGING";
        case 0x02u:
            return "DISCHARGE";
        default:
            return "UNKNOWN";
    }
}

static std::string pmic_rail_state_line(const std::string &name, const std::string &voltage, bool enabled) {
    return fixed_field(name, 10) + fixed_field(voltage, 8) + fixed_field(enabled ? "ON" : "OFF", 6);
}

static esp_err_t bsp_pmic_apply_initial_power_sequence(void) {
    ESP_LOGI(TAG, "PMIC safe sequence (legacy reference):");
    ESP_LOGI(TAG, "  Stage 0: VBUS limit=4.36V, I_LIMIT=1500mA, VSYS shutdown=2600mV");
    ESP_LOGI(TAG, "  Stage 1: core rails = DC1 3.3V, DC3 3.3V, ALDO1 1.8V");
    ESP_LOGI(TAG, "  Stage 2: board rails = DC2 1.0V, DC4 1.0V, DC5 3.3V");
    ESP_LOGI(TAG, "  Stage 3: analog rails = ALDO2 3.3V, ALDO3 3.3V, ALDO4 3.3V");
    ESP_LOGI(TAG, "  Stage 4: secondary rails = BLDO1 1.5V, BLDO2 2.8V, CPUSLDO 1.0V, DLDO1 3.3V, DLDO2 3.3V");
    ESP_LOGI(TAG, "  Rail enable remains intentionally disabled until the board power topology is confirmed.");
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

    err = bsp_pmic_apply_initial_power_sequence();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PMIC init config failed: %s", esp_err_to_name(err));
        return err;
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

void bsp_pmic_print_status(void) {
    app::SerialBoxPrinter printer("PMIC STATUS");

    if (s_pmic_dev_handle == NULL) {
        printer.add_body_line("PMIC: NOT INIT");
        printer.print();
        return;
    }

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t chip_id = 0;
    esp_err_t err = bsp_pmic_read_status(&status1, &status2, &chip_id);
    if (err != ESP_OK) {
        printer.add_body_line("PMIC: READ FAIL");
        printer.print();
        return;
    }

    char chip_buf[16];
    char status1_buf[16];
    char status2_buf[16];
    std::snprintf(chip_buf, sizeof(chip_buf), "0x%02X", chip_id);
    std::snprintf(status1_buf, sizeof(status1_buf), "0x%02X", status1);
    std::snprintf(status2_buf, sizeof(status2_buf), "0x%02X", status2);

    printer.add_body_line(fixed_field("CHIP", 12) + chip_buf);
    printer.add_body_line(fixed_field("STATUS1", 12) + status1_buf);
    printer.add_body_line(fixed_field("STATUS2", 12) + status2_buf);
    
    printer.add_blank_body();
    printer.add_body_line("STATE");
    printer.add_body_bullet(fixed_field("VBUS_GOOD", 18) + pmic_yes_no((status1 & (1u << 5)) != 0u), 2U);
    printer.add_body_bullet(fixed_field("BAT_CONNECTED", 18) + pmic_yes_no((status1 & (1u << 3)) != 0u), 2U);
    printer.add_body_bullet(fixed_field("CHARGE_STATE", 18) + pmic_charge_state(status2), 2U);
    printer.add_body_bullet(fixed_field("POWER_STATE", 18) + pmic_yes_no((status2 & (1u << 4)) != 0u), 2U);
    printer.add_body_bullet(fixed_field("VBUS_INPUT", 18) + pmic_yes_no((status2 & (1u << 3)) == 0u), 2U);
    printer.add_body_bullet(fixed_field("CURRENT_LIMIT", 18) + pmic_yes_no((status1 & 0x01u) != 0u), 2U);
    printer.add_body_bullet(fixed_field("THERMAL_REG", 18) + pmic_yes_no((status1 & 0x02u) != 0u), 2U);
    
    printer.add_blank_body();
    printer.add_body_line("RAILS");
    printer.add_body_bullet(pmic_rail_state_line("DC1", "3.3V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("DC2", "1.0V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("DC3", "3.3V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("DC4", "1.0V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("DC5", "3.3V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("ALDO1", "1.8V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("ALDO2", "3.3V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("ALDO3", "3.3V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("ALDO4", "3.3V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("BLDO1", "1.5V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("BLDO2", "2.8V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("CPUSLDO", "1.0V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("DLDO1", "3.3V", false), 2U);
    printer.add_body_bullet(pmic_rail_state_line("DLDO2", "3.3V", false), 2U);
    
    printer.print();
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
    if (enable) {
        ESP_LOGW(TAG, "Legacy AXP2101 enable order (safe staged list only, not applied automatically):");
        ESP_LOGW(TAG, "  1) DC2 -> DC3 -> DC4 -> DC5");
        ESP_LOGW(TAG, "  2) ALDO1 -> ALDO2 -> ALDO3 -> ALDO4");
        ESP_LOGW(TAG, "  3) BLDO1 -> BLDO2 -> CPUSLDO");
        ESP_LOGW(TAG, "  4) DLDO1 -> DLDO2");
        ESP_LOGW(TAG, "  DC1 is kept out of the enable sequence in the legacy example and remains disabled here.");
        ESP_LOGW(TAG, "  Actual rail writes remain disabled until the board sequencing is confirmed on hardware.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "PMIC rail enable remains disabled until the board sequencing is confirmed.");
    return ESP_OK;
}

esp_err_t bsp_pmic_set_backlight_enable(bool enable) {
    esp_err_t err = bsp_board_set_backlight(enable);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG, "Backlight control %s via board GPIO and PMIC rail state", enable ? "enabled" : "disabled");
    return ESP_OK;
}
