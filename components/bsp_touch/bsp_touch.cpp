#include "bsp_touch.h"

#include "bsp_board.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "serial_box_printer.h"

#include <cstdio>
#include <string>

static esp_lcd_panel_io_handle_t s_touch_io = nullptr;
static esp_lcd_touch_handle_t s_touch = nullptr;
static uint16_t s_touch_width = 0;
static uint16_t s_touch_height = 0;
static constexpr uint32_t TOUCH_I2C_CLOCK_HZ = 400000U;

esp_err_t bsp_touch_init(uint16_t width, uint16_t height) {
    if (s_touch != nullptr) {
        return ESP_OK;
    }
    if (width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_master_bus_handle_t bus = bsp_board_get_i2c_bus_handle();
    if (bus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_lcd_panel_io_i2c_config_t io_config = {};
    io_config.dev_addr = ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS;
    io_config.scl_speed_hz = TOUCH_I2C_CLOCK_HZ;
    io_config.control_phase_bytes = 1;
    io_config.dc_bit_offset = 0;
    io_config.lcd_cmd_bits = 8;
    io_config.flags.disable_control_phase = true;
    esp_err_t err = esp_lcd_new_panel_io_i2c(bus, &io_config, &s_touch_io);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_touch_config_t touch_config = {};
    touch_config.x_max = static_cast<uint16_t>(width - 1U);
    touch_config.y_max = static_cast<uint16_t>(height - 1U);
    touch_config.rst_gpio_num = GPIO_NUM_NC;
    touch_config.int_gpio_num = GPIO_NUM_NC;
    err = esp_lcd_touch_new_i2c_axs15231b(s_touch_io, &touch_config, &s_touch);
    if (err != ESP_OK) {
        esp_lcd_panel_io_del(s_touch_io);
        s_touch_io = nullptr;
        return err;
    }

    s_touch_width = width;
    s_touch_height = height;
    return ESP_OK;
}

void bsp_touch_print_status(void) {
    if (s_touch == nullptr) {
        return;
    }

    const bsp_board_config_t *board_config = bsp_board_get_config();
    char bus_info[96];
    char device_info[96];
    char gpio_info[96];
    std::snprintf(bus_info, sizeof(bus_info), "I2C%d SDA=GPIO%d SCL=GPIO%d",
                  board_config->i2c_port, static_cast<int>(board_config->i2c_sda_gpio),
                  static_cast<int>(board_config->i2c_scl_gpio));
    std::snprintf(device_info, sizeof(device_info), "Address 0x%02X, device clock %u kHz",
                  static_cast<unsigned>(ESP_LCD_TOUCH_IO_I2C_AXS15231B_ADDRESS),
                  static_cast<unsigned>(TOUCH_I2C_CLOCK_HZ / 1000U));
    std::snprintf(gpio_info, sizeof(gpio_info), "INT=%s, RST=%s",
                  board_config->touch_int_gpio == GPIO_NUM_NC ? "NC" : "routed",
                  board_config->touch_rst_gpio == GPIO_NUM_NC ? "NC" : "routed");

    app::SerialBoxPrinter printer("TOUCH STATUS");
    printer.add_body_bullet("Controller: AXS15231B initialized", 2U);
    printer.add_body_bullet("Bus: " + std::string(bus_info), 2U);
    printer.add_body_bullet("Device: " + std::string(device_info), 2U);
    printer.add_body_bullet("Coordinates: X=0.." + std::to_string(s_touch_width - 1U) +
                            ", Y=0.." + std::to_string(s_touch_height - 1U), 2U);
    printer.add_body_bullet("Read capacity: 1 touch point per call", 2U);
    printer.add_body_bullet("GPIO: " + std::string(gpio_info), 2U);
    printer.add_body_bullet("Input: application-polled", 2U);
    printer.print();
}

esp_err_t bsp_touch_deinit(void) {
    esp_err_t first_err = ESP_OK;
    if (s_touch != nullptr) {
        first_err = esp_lcd_touch_del(s_touch);
        if (first_err == ESP_OK) {
            s_touch = nullptr;
        }
    }
    if (s_touch_io != nullptr) {
        esp_err_t err = esp_lcd_panel_io_del(s_touch_io);
        if (first_err == ESP_OK) {
            first_err = err;
        }
        if (err == ESP_OK) {
            s_touch_io = nullptr;
        }
    }
    return first_err;
}

esp_err_t bsp_touch_read(bsp_touch_point_t *point) {
    if (point == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    *point = {};
    if (s_touch == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_lcd_touch_read_data(s_touch);
    if (err != ESP_OK) {
        return err;
    }

    esp_lcd_touch_point_data_t coordinates[1] = {};
    uint8_t point_count = 0;
    err = esp_lcd_touch_get_data(s_touch, coordinates, &point_count, 1);
    if (err != ESP_OK) {
        return err;
    }
    if (point_count > 0) {
        point->x = coordinates[0].x;
        point->y = coordinates[0].y;
        point->pressed = true;
    }
    return ESP_OK;
}

esp_err_t bsp_touch_reset(void) {
    return ESP_ERR_NOT_SUPPORTED;
}
