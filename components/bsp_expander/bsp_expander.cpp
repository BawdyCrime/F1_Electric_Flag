#include "bsp_expander.h"

#include "bsp_board.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_box.h"

#include <string>

static const char *TAG = "bsp_expander";
static esp_io_expander_handle_t s_expander = nullptr;

static bool valid_pin_mask(uint32_t pin_mask) {
    return pin_mask != 0 && (pin_mask & ~0xFFU) == 0;
}

esp_err_t bsp_expander_init(void) {
    if (s_expander != nullptr) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t i2c_bus = bsp_board_get_i2c_bus_handle();
    if (i2c_bus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_io_expander_new_i2c_tca9554(
        i2c_bus, ESP_IO_EXPANDER_I2C_TCA9554_ADDRESS_000, &s_expander);
}

esp_err_t bsp_expander_set_direction(uint32_t pin_mask, bool output) {
    if (!valid_pin_mask(pin_mask)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_expander == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_io_expander_set_dir(s_expander, pin_mask,
                                   output ? IO_EXPANDER_OUTPUT : IO_EXPANDER_INPUT);
}

esp_err_t bsp_expander_write(uint32_t pin_mask, bool high) {
    if (!valid_pin_mask(pin_mask)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_expander == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_io_expander_set_level(s_expander, pin_mask, high ? 1 : 0);
}

esp_err_t bsp_expander_read(uint32_t pin_mask, uint32_t *level_mask) {
    if (!valid_pin_mask(pin_mask) || level_mask == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_expander == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_io_expander_get_level(s_expander, pin_mask, level_mask);
}

esp_err_t bsp_expander_pulse_lcd_reset(void) {
    if (s_expander == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = bsp_expander_set_direction(BSP_EXPANDER_LCD_RESET, true);
    if (err == ESP_OK) {
        err = bsp_expander_write(BSP_EXPANDER_LCD_RESET, false);
    }
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    err = bsp_expander_write(BSP_EXPANDER_LCD_RESET, true);
    if (err == ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    return err;
}

void bsp_expander_print_status(void) {
    if (s_expander == nullptr) {
        ESP_LOGE(TAG, "Cannot print expander status before initialization");
        return;
    }

    uint32_t input_levels = 0;
    uint32_t output_latch = 0;
    uint32_t direction = 0;
    esp_err_t err = s_expander->read_input_reg(s_expander, &input_levels);
    if (err == ESP_OK) {
        err = s_expander->read_output_reg(s_expander, &output_latch);
    }
    if (err == ESP_OK) {
        err = s_expander->read_direction_reg(s_expander, &direction);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read expander status: %s", esp_err_to_name(err));
        return;
    }

    app::SerialBoxPrinter printer("TCA9554 EXPANDER");
    printer.add_body_line(app::pad_field("PORT", 6) + app::pad_field("FUNCTION", 14) +
                          app::pad_field("DIR", 5) + app::pad_field("LEVEL", 7) + "LATCH");
    for (uint32_t pin = 0; pin < 8; ++pin) {
        const uint32_t pin_mask = 1U << pin;
        const bool output = s_expander->config.flags.dir_out_bit_zero
                                ? (direction & pin_mask) == 0
                                : (direction & pin_mask) != 0;
        const std::string function = (pin_mask == BSP_EXPANDER_LCD_RESET) ? "LCD RESET" : "UNASSIGNED";
        const std::string row = app::pad_field("P" + std::to_string(pin), 6) + app::pad_field(function, 14) +
                                app::pad_field(output ? "OUT" : "IN", 5) +
                                app::pad_field((input_levels & pin_mask) ? "HIGH" : "LOW", 7) +
                                ((output_latch & pin_mask) ? "HIGH" : "LOW");
        printer.add_body_line(row);
    }
    printer.print();
}

esp_err_t bsp_expander_deinit(void) {
    if (s_expander == nullptr) {
        return ESP_OK;
    }
    esp_err_t err = esp_io_expander_del(s_expander);
    if (err == ESP_OK) {
        s_expander = nullptr;
    }
    return err;
}