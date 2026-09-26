#include "bsp_board.h"

#include <algorithm>
#include <stdio.h>
#include <string>
#include <vector>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "serial_box_printer.h"

static const char *TAG = "bsp_board";
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static std::vector<uint8_t> s_scanned_i2c_devices;
static const bsp_board_config_t s_board_config = {
    .i2c_port = BSP_I2C_PORT,
    .i2c_sda_gpio = BSP_I2C_SDA_GPIO,
    .i2c_scl_gpio = BSP_I2C_SCL_GPIO,
    .i2c_clock_hz = BSP_I2C_CLOCK_HZ,
    .status_led_gpio = BSP_STATUS_LED_GPIO,
    .touch_int_gpio = BSP_TOUCH_INT_GPIO,
    .touch_rst_gpio = BSP_TOUCH_RST_GPIO,
    .lcd_bl_gpio = BSP_LCD_BL_GPIO,

    .spi_host = BSP_SPI_HOST,
    .spi_sclk_gpio = BSP_SPI_SCLK_GPIO,
    .spi_mosi_gpio = BSP_SPI_MOSI_GPIO,
    .spi_miso_gpio = BSP_SPI_MISO_GPIO,
    .spi_cs_gpio = BSP_SPI_CS_GPIO,
    .spi_qspi_io0_gpio = BSP_SPI_QSPI_IO0_GPIO,
    .spi_qspi_io1_gpio = BSP_SPI_QSPI_IO1_GPIO,
    .spi_qspi_io2_gpio = BSP_SPI_QSPI_IO2_GPIO,
    .spi_qspi_io3_gpio = BSP_SPI_QSPI_IO3_GPIO,
    .spi_clock_hz = BSP_SPI_CLOCK_HZ,

    .pmic_i2c_addr_1 = 0x34,
    .pmic_i2c_addr_2 = 0x35,
    .imu_i2c_addr = 0x6B,
    .rtc_i2c_addr = 0x51,
};

namespace {

std::string gpio_label(gpio_num_t gpio) {
    if (gpio == GPIO_NUM_NC) {
        return "NC";
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "GPIO%d", static_cast<int>(gpio));
    return buffer;
}

std::string gpio_state_line(const std::string &name, gpio_num_t gpio, const std::string &state, const std::string &level = "") {
    std::string line = app::pad_field(name, 12) + app::pad_field(gpio_label(gpio), 10) + app::pad_field(state, 10);
    if (!level.empty()) {
        line += level;
    }
    return line;
}

std::string hex_byte(uint8_t value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "0x%02X", value);
    return buffer;
}

std::string i2c_devices_summary_string() {
    if (s_scanned_i2c_devices.empty()) {
        return "NONE";
    }

    std::string result;
    for (size_t i = 0; i < s_scanned_i2c_devices.size(); ++i) {
        const std::string address = hex_byte(s_scanned_i2c_devices[i]);
        result += (i + 1U < s_scanned_i2c_devices.size()) ? app::pad_field(address, 6U) : address;
    }
    return result;
}

}  // namespace

const bsp_board_config_t *bsp_board_get_config(void) {
    return &s_board_config;
}

i2c_master_bus_handle_t bsp_board_get_i2c_bus_handle(void) {
    return s_i2c_bus_handle;
}

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

    err = bsp_board_configure_gpio(BSP_TOUCH_INT_GPIO, false, true, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch interrupt GPIO config failed: %s", esp_err_to_name(err));
        return err;
    }

    if (BSP_TOUCH_RST_GPIO != GPIO_NUM_NC) {
        err = bsp_board_configure_gpio(BSP_TOUCH_RST_GPIO, true, false, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "touch reset GPIO config failed: %s", esp_err_to_name(err));
            return err;
        }
        gpio_set_level(BSP_TOUCH_RST_GPIO, 1);
    }

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

    if (BSP_SPI_CS_GPIO != GPIO_NUM_NC) {
        err = bsp_board_configure_gpio(BSP_SPI_CS_GPIO, true, false, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SPI CS GPIO config failed: %s", esp_err_to_name(err));
            return err;
        }
        gpio_set_level(BSP_SPI_CS_GPIO, 1);
    }

    if (BSP_LCD_BL_GPIO != GPIO_NUM_NC) {
        err = bsp_board_configure_gpio(BSP_LCD_BL_GPIO, true, false, false);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LCD backlight GPIO config failed: %s", esp_err_to_name(err));
            return err;
        }
        gpio_set_level(BSP_LCD_BL_GPIO, 0);
    }

    return ESP_OK;
}

esp_err_t bsp_board_i2c_scan(void) {
    s_scanned_i2c_devices.clear();

    bool found_any = false;
    const uint8_t pmic_candidates[] = {0x34, 0x35};
    const uint8_t imu_candidates[] = {0x6A, 0x6B};

    if (s_i2c_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus is not initialized; call bsp_board_init() first.");
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t addr = 0; addr < 0x80; ++addr) {
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, addr, 1000);
        if (err == ESP_OK) {
            found_any = true;
            if (std::find(s_scanned_i2c_devices.begin(), s_scanned_i2c_devices.end(), addr) == s_scanned_i2c_devices.end()) {
                s_scanned_i2c_devices.push_back(addr);
            }
        }
    }

    if (!found_any) {
        return ESP_ERR_NOT_FOUND;
    }

    for (size_t i = 0; i < sizeof(pmic_candidates); ++i) {
        if (i2c_master_probe(s_i2c_bus_handle, pmic_candidates[i], 1000) == ESP_OK) {
            if (std::find(s_scanned_i2c_devices.begin(), s_scanned_i2c_devices.end(), pmic_candidates[i]) == s_scanned_i2c_devices.end()) {
                s_scanned_i2c_devices.push_back(pmic_candidates[i]);
            }
        }
    }

    for (size_t i = 0; i < sizeof(imu_candidates); ++i) {
        if (i2c_master_probe(s_i2c_bus_handle, imu_candidates[i], 1000) == ESP_OK) {
            if (std::find(s_scanned_i2c_devices.begin(), s_scanned_i2c_devices.end(), imu_candidates[i]) == s_scanned_i2c_devices.end()) {
                s_scanned_i2c_devices.push_back(imu_candidates[i]);
            }
        }
    }

    std::sort(s_scanned_i2c_devices.begin(), s_scanned_i2c_devices.end());
    return ESP_OK;
}

void bsp_board_print_peripheral(void) {
    const bsp_board_config_t &config = *bsp_board_get_config();
    app::SerialBoxPrinter printer("BOARD PERIPHERALS");

    const std::string spi_host_name = (config.spi_host == SPI2_HOST) ? "SPI2" : "SPI1";
    const std::string touch_int_state = (config.touch_int_gpio == GPIO_NUM_NC) ? "—" : "INPUT";
    const std::string touch_rst_state = (config.touch_rst_gpio == GPIO_NUM_NC) ? "—" : "OUTPUT";
    const std::string status_led_state = (config.status_led_gpio == GPIO_NUM_NC) ? "—" : "OUTPUT";
    const std::string backlight_state = (config.lcd_bl_gpio == GPIO_NUM_NC) ? "—" : "OUTPUT";
    const std::string chip_select_state = (config.spi_cs_gpio == GPIO_NUM_NC) ? "—" : "OUTPUT";

    const std::string i2c_clock_hz = std::to_string(config.i2c_clock_hz / 1000U) + " kHz";
    const std::string spi_clock_hz = std::to_string(config.spi_clock_hz / 1000000U) + " MHz";
    const std::string pmic_addrs = hex_byte(config.pmic_i2c_addr_1) + " / " + hex_byte(config.pmic_i2c_addr_2);
    const std::string imu_addr = hex_byte(config.imu_i2c_addr);
    const std::string rtc_addr = hex_byte(config.rtc_i2c_addr);
    const std::string device_list = i2c_devices_summary_string();

    const std::string on_board_label = app::pad_field("DISPLAY", 12);
    const std::string touch_label = app::pad_field("TOUCH", 12);
    const std::string imu_label = app::pad_field("IMU", 12);
    const std::string rtc_label = app::pad_field("RTC", 12);
    const std::string pmic_label = app::pad_field("PMIC", 12);
    const std::string audio_label = app::pad_field("AUDIO", 12);

    const std::string i2c_bus_label = app::pad_field("Bus", 12);
    const std::string sda_label = app::pad_field("SDA", 12);
    const std::string scl_label = app::pad_field("SCL", 12);
    const std::string clock_label = app::pad_field("Clock", 12);
    const std::string devices_label = app::pad_field("Devices", 12);
    const std::string state_label = app::pad_field("State", 12);

    const std::string display_bus_label = app::pad_field("Host", 12);
    const std::string mode_label = app::pad_field("Mode", 12);
    const std::string d0_label = app::pad_field("D0", 12);
    const std::string d1_label = app::pad_field("D1", 12);
    const std::string d2_label = app::pad_field("D2", 12);
    const std::string d3_label = app::pad_field("D3", 12);
    const std::string cs_label = app::pad_field("CS", 12);

    printer.add_body_line("ONBOARD");
    printer.add_body_bullet(on_board_label + app::pad_field("AXS15231B", 16) + app::pad_field("QSPI", 8), 2U);
    printer.add_body_bullet(touch_label + app::pad_field("AXS15231B", 16) + app::pad_field("I2C", 8), 2U);
    printer.add_body_bullet(imu_label + app::pad_field("QMI8658", 16) + app::pad_field("I2C", 8) + imu_addr, 2U);
    printer.add_body_bullet(rtc_label + app::pad_field("PCF85063", 16) + app::pad_field("I2C", 8) + rtc_addr, 2U);
    printer.add_body_bullet(pmic_label + app::pad_field("AXP2101", 16) + app::pad_field("I2C", 8) + pmic_addrs, 2U);
    printer.add_body_bullet(audio_label + app::pad_field("ES8311", 16) + app::pad_field("I2C", 8), 2U);
    printer.add_body_bullet(app::pad_field("AUDIO I2S", 12) + "NOT CONFIGURED", 2U);

    printer.add_blank_body();
    printer.add_body_line("I2C BUS");
    printer.add_body_bullet(i2c_bus_label + "I2C" + std::to_string(config.i2c_port), 2U);
    printer.add_body_bullet(sda_label + "GPIO" + std::to_string(config.i2c_sda_gpio), 2U);
    printer.add_body_bullet(scl_label + "GPIO" + std::to_string(config.i2c_scl_gpio), 2U);
    printer.add_body_bullet(clock_label + i2c_clock_hz, 2U);
    printer.add_body_bullet(devices_label + device_list, 2U);
    printer.add_body_bullet(state_label + "READY", 2U);

    printer.add_blank_body();
    printer.add_body_line("SPI BUS");
    printer.add_body_bullet(display_bus_label + spi_host_name, 2U);
    printer.add_body_bullet(mode_label + "QSPI", 2U);
    printer.add_body_bullet(clock_label + spi_clock_hz, 2U);
    printer.add_body_bullet(cs_label + "GPIO" + std::to_string(config.spi_cs_gpio), 2U);
    printer.add_body_bullet(d0_label + "GPIO" + std::to_string(config.spi_qspi_io0_gpio), 2U);
    printer.add_body_bullet(d1_label + "GPIO" + std::to_string(config.spi_qspi_io1_gpio), 2U);
    printer.add_body_bullet(d2_label + "GPIO" + std::to_string(config.spi_qspi_io2_gpio), 2U);
    printer.add_body_bullet(d3_label + "GPIO" + std::to_string(config.spi_qspi_io3_gpio), 2U);

    printer.add_blank_body();
    printer.add_body_line("GPIO");
    printer.add_body_bullet(gpio_state_line("SPI CS", config.spi_cs_gpio, chip_select_state,
                                            config.spi_cs_gpio == GPIO_NUM_NC ? "" : "HIGH"), 2U);
    printer.add_body_bullet(gpio_state_line("LCD BL", config.lcd_bl_gpio, backlight_state,
                                            config.lcd_bl_gpio == GPIO_NUM_NC ? "" : "LOW"), 2U);
    printer.add_body_bullet(gpio_state_line("TOUCH INT", config.touch_int_gpio, touch_int_state), 2U);
    printer.add_body_bullet(gpio_state_line("TOUCH RST", config.touch_rst_gpio, touch_rst_state,
                                            config.touch_rst_gpio == GPIO_NUM_NC ? "" : "HIGH"), 2U);
    printer.add_body_bullet(gpio_state_line("STATUS LED", config.status_led_gpio, status_led_state), 2U);

    printer.print();
}

esp_err_t bsp_board_set_backlight(bool enabled) {
    if (BSP_LCD_BL_GPIO == GPIO_NUM_NC) {
        return ESP_OK;
    }

    esp_err_t err = bsp_board_configure_gpio(BSP_LCD_BL_GPIO, true, false, false);
    if (err != ESP_OK) {
        return err;
    }
    gpio_set_level(BSP_LCD_BL_GPIO, enabled ? 1 : 0);
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
    return ESP_OK;
}
