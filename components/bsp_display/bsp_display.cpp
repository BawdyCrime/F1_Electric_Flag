#include "bsp_display.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"

#include "bsp_board.h"
#include "serial_box_printer.h"

static const char *TAG = "bsp_display";
static esp_lcd_panel_io_handle_t s_panel_io = nullptr;
static esp_lcd_panel_handle_t s_panel = nullptr;
static lv_display_t *s_lvgl_display = nullptr;
static bool s_spi_bus_initialized = false;
static bool s_lvgl_initialized = false;
static constexpr int kLvglDrawBufferRows = BSP_DISPLAY_PANEL_HEIGHT / 4;

#define AXS_INIT_CMD(command, delay, ...) \
    { command, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}), delay }
#define AXS_INIT_CMD_NO_DATA(command, delay) { command, nullptr, 0, delay }

static const axs15231b_lcd_init_cmd_t s_board_init_cmds[] = {
    AXS_INIT_CMD(0xBB, 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5A, 0xA5),
    AXS_INIT_CMD(0xA0, 0, 0xC0, 0x10, 0x00, 0x02, 0x00, 0x00, 0x04, 0x3F, 0x20, 0x05, 0x3F, 0x3F, 0x00, 0x00, 0x00, 0x00, 0x00),
    AXS_INIT_CMD(0xA2, 0, 0x30, 0x3C, 0x24, 0x14, 0xD0, 0x20, 0xFF, 0xE0, 0x40, 0x19, 0x80, 0x80, 0x80, 0x20, 0xF9, 0x10, 0x02, 0xFF, 0xFF, 0xF0, 0x90, 0x01, 0x32, 0xA0, 0x91, 0xE0, 0x20, 0x7F, 0xFF, 0x00, 0x5A),
    AXS_INIT_CMD(0xD0, 0, 0xE0, 0x40, 0x51, 0x24, 0x08, 0x05, 0x10, 0x01, 0x20, 0x15, 0x42, 0xC2, 0x22, 0x22, 0xAA, 0x03, 0x10, 0x12, 0x60, 0x14, 0x1E, 0x51, 0x15, 0x00, 0x8A, 0x20, 0x00, 0x03, 0x3A, 0x12),
    AXS_INIT_CMD(0xA3, 0, 0xA0, 0x06, 0xAA, 0x00, 0x08, 0x02, 0x0A, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x55, 0x55),
    AXS_INIT_CMD(0xC1, 0, 0x31, 0x04, 0x02, 0x02, 0x71, 0x05, 0x24, 0x55, 0x02, 0x00, 0x41, 0x00, 0x53, 0xFF, 0xFF, 0xFF, 0x4F, 0x52, 0x00, 0x4F, 0x52, 0x00, 0x45, 0x3B, 0x0B, 0x02, 0x0D, 0x00, 0xFF, 0x40),
    AXS_INIT_CMD(0xC3, 0, 0x00, 0x00, 0x00, 0x50, 0x03, 0x00, 0x00, 0x00, 0x01, 0x80, 0x01),
    AXS_INIT_CMD(0xC4, 0, 0x00, 0x24, 0x33, 0x80, 0x00, 0xEA, 0x64, 0x32, 0xC8, 0x64, 0xC8, 0x32, 0x90, 0x90, 0x11, 0x06, 0xDC, 0xFA, 0x00, 0x00, 0x80, 0xFE, 0x10, 0x10, 0x00, 0x0A, 0x0A, 0x44, 0x50),
    AXS_INIT_CMD(0xC5, 0, 0x18, 0x00, 0x00, 0x03, 0xFE, 0x3A, 0x4A, 0x20, 0x30, 0x10, 0x88, 0xDE, 0x0D, 0x08, 0x0F, 0x0F, 0x01, 0x3A, 0x4A, 0x20, 0x10, 0x10, 0x00),
    AXS_INIT_CMD(0xC6, 0, 0x05, 0x0A, 0x05, 0x0A, 0x00, 0xE0, 0x2E, 0x0B, 0x12, 0x22, 0x12, 0x22, 0x01, 0x03, 0x00, 0x3F, 0x6A, 0x18, 0xC8, 0x22),
    AXS_INIT_CMD(0xC7, 0, 0x50, 0x32, 0x28, 0x00, 0xA2, 0x80, 0x8F, 0x00, 0x80, 0xFF, 0x07, 0x11, 0x9C, 0x67, 0xFF, 0x24, 0x0C, 0x0D, 0x0E, 0x0F),
    AXS_INIT_CMD(0xC9, 0, 0x33, 0x44, 0x44, 0x01),
    AXS_INIT_CMD(0xCF, 0, 0x2C, 0x1E, 0x88, 0x58, 0x13, 0x18, 0x56, 0x18, 0x1E, 0x68, 0x88, 0x00, 0x65, 0x09, 0x22, 0xC4, 0x0C, 0x77, 0x22, 0x44, 0xAA, 0x55, 0x08, 0x08, 0x12, 0xA0, 0x08),
    AXS_INIT_CMD(0xD5, 0, 0x40, 0x8E, 0x8D, 0x01, 0x35, 0x04, 0x92, 0x74, 0x04, 0x92, 0x74, 0x04, 0x08, 0x6A, 0x04, 0x46, 0x03, 0x03, 0x03, 0x03, 0x82, 0x01, 0x03, 0x00, 0xE0, 0x51, 0xA1, 0x00, 0x00, 0x00),
    AXS_INIT_CMD(0xD6, 0, 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE, 0x93, 0x00, 0x01, 0x83, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x84, 0x00, 0x20, 0x01, 0x00),
    AXS_INIT_CMD(0xD7, 0, 0x03, 0x01, 0x0B, 0x09, 0x0F, 0x0D, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19, 0x40, 0x8E, 0x04, 0x00, 0x20, 0xA0, 0x1F),
    AXS_INIT_CMD(0xD8, 0, 0x02, 0x00, 0x0A, 0x08, 0x0E, 0x0C, 0x1E, 0x1F, 0x18, 0x1D, 0x1F, 0x19),
    AXS_INIT_CMD(0xD9, 0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F),
    AXS_INIT_CMD(0xDD, 0, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F, 0x1F),
    AXS_INIT_CMD(0xDF, 0, 0x44, 0x73, 0x4B, 0x69, 0x00, 0x0A, 0x02, 0x90),
    AXS_INIT_CMD(0xE0, 0, 0x3B, 0x28, 0x10, 0x16, 0x0C, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x13, 0x2C, 0x33, 0x28, 0x0D),
    AXS_INIT_CMD(0xE1, 0, 0x37, 0x28, 0x10, 0x16, 0x0B, 0x06, 0x11, 0x28, 0x5C, 0x21, 0x0D, 0x35, 0x14, 0x2C, 0x33, 0x28, 0x0F),
    AXS_INIT_CMD(0xE2, 0, 0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D),
    AXS_INIT_CMD(0xE3, 0, 0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x35, 0x44, 0x32, 0x0C, 0x14, 0x14, 0x36, 0x32, 0x2F, 0x0F),
    AXS_INIT_CMD(0xE4, 0, 0x3B, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0D),
    AXS_INIT_CMD(0xE5, 0, 0x37, 0x07, 0x12, 0x18, 0x0E, 0x0D, 0x17, 0x39, 0x44, 0x2E, 0x0C, 0x14, 0x14, 0x36, 0x3A, 0x2F, 0x0F),
    AXS_INIT_CMD(0xA4, 0, 0x85, 0x85, 0x95, 0x82, 0xAF, 0xAA, 0xAA, 0x80, 0x10, 0x30, 0x40, 0x40, 0x20, 0xFF, 0x60, 0x30),
    AXS_INIT_CMD(0xA4, 0, 0x85, 0x85, 0x95, 0x85),
    AXS_INIT_CMD(0xBB, 0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00),
    AXS_INIT_CMD_NO_DATA(0x13, 0),
    AXS_INIT_CMD_NO_DATA(0x11, 120),
    AXS_INIT_CMD(0x2C, 0, 0x00, 0x00, 0x00, 0x00),
};

#undef AXS_INIT_CMD_NO_DATA
#undef AXS_INIT_CMD

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
    bus_cfg.max_transfer_sz = BSP_DISPLAY_PANEL_WIDTH * kLvglDrawBufferRows * sizeof(uint16_t);

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
    vendor_config.init_cmds = s_board_init_cmds;
    vendor_config.init_cmds_size = sizeof(s_board_init_cmds) / sizeof(s_board_init_cmds[0]);
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

    char display_info[128];
    std::snprintf(display_info, sizeof(display_info), "Host=%d, SCLK=%d, D0..D3=%d/%d/%d/%d, CS=%d",
                  BSP_SPI_HOST, BSP_SPI_SCLK_GPIO, BSP_SPI_QSPI_IO0_GPIO, BSP_SPI_QSPI_IO1_GPIO,
                  BSP_SPI_QSPI_IO2_GPIO, BSP_SPI_QSPI_IO3_GPIO, BSP_SPI_CS_GPIO);
    app::SerialBoxPrinter printer("DISPLAY STATUS");
    printer.add_body_bullet("AXS15231B initialized; backlight enabled", 2U);
    printer.add_body_bullet(display_info, 2U);
    printer.print();
    return ESP_OK;
}

esp_err_t bsp_display_deinit(void) {
    esp_err_t first_err = ESP_OK;
    if (s_lvgl_display != nullptr) {
        esp_err_t err = lvgl_port_remove_disp(s_lvgl_display);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
        s_lvgl_display = nullptr;
    }
    if (s_lvgl_initialized) {
        esp_err_t err = lvgl_port_deinit();
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
        s_lvgl_initialized = false;
    }
    esp_err_t backlight_err = bsp_display_set_backlight(false);
    if (first_err == ESP_OK && backlight_err != ESP_OK) {
        first_err = backlight_err;
    }
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
    return ESP_OK;
}

esp_err_t bsp_display_set_backlight(bool enabled) {
    return bsp_board_set_backlight(enabled);
}

esp_err_t bsp_display_lvgl_init(void) {
    if (s_lvgl_display != nullptr) {
        return ESP_OK;
    }
    if (s_panel == nullptr || s_panel_io == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    esp_err_t err = lvgl_port_init(&lvgl_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(err));
        return err;
    }
    s_lvgl_initialized = true;

    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = s_panel_io,
        .panel_handle = s_panel,
        .control_handle = nullptr,
        .buffer_size = BSP_DISPLAY_PANEL_WIDTH * kLvglDrawBufferRows,
        .double_buffer = true,
        .trans_size = 0,
        .hres = BSP_DISPLAY_PANEL_WIDTH,
        .vres = BSP_DISPLAY_PANEL_HEIGHT,
        .monochrome = false,
        .rotation = {},
        .rounder_cb = nullptr,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .flags = {
            .buff_dma = true,
            .buff_spiram = false,
            .sw_rotate = false,
            .swap_bytes = true,
            .full_refresh = false,
            .direct_mode = false,
        },
    };
    s_lvgl_display = lvgl_port_add_disp(&display_cfg);
    if (s_lvgl_display == nullptr) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        lvgl_port_deinit();
        s_lvgl_initialized = false;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t bsp_display_set_rotation(uint16_t rotation) {
    (void)rotation;
    return ESP_OK;
}
