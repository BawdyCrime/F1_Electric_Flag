#include "bsp_touch.h"

#include "bsp_board.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "serial_box_printer.h"

static esp_lcd_panel_io_handle_t s_touch_io = nullptr;
static esp_lcd_touch_handle_t s_touch = nullptr;

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
    io_config.scl_speed_hz = 400000;
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

    app::SerialBoxPrinter printer("TOUCH STATUS");
    printer.add_body_bullet("Controller: AXS15231B", 2U);
    printer.add_body_bullet("I2C address: 0x3B at 400 kHz", 2U);
    printer.add_body_bullet("Polling enabled; interrupt/reset pins are not routed", 2U);
    printer.print();
    return ESP_OK;
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
