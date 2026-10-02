#include "bsp_display.h"

#include "esp_log.h"
#include "driver/spi_master.h"
#include "esp_lcd_axs15231b.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"

#include "bsp_board.h"
#include "serial_box.h"

#include "esp_heap_caps.h"

#include <algorithm>
#include <cstdio>

static const char *TAG = "bsp_display";
static esp_lcd_panel_io_handle_t s_panel_io = nullptr;
static esp_lcd_panel_handle_t s_panel = nullptr;
static lv_display_t *s_lvgl_display = nullptr;
static bool s_spi_bus_initialized = false;
static size_t s_init_command_count = 0;

extern "C" const axs15231b_lcd_init_cmd_t *bsp_display_get_init_commands(size_t *count);

static esp_err_t display_direct_color_test(void) {
    constexpr uint32_t rows_per_chunk = BSP_DISPLAY_PANEL_HEIGHT/4;
    constexpr uint16_t test_color = 0x07E0;
    const size_t pixel_count = BSP_DISPLAY_PANEL_WIDTH * rows_per_chunk;
    auto *pixels = static_cast<uint16_t *>(heap_caps_malloc(pixel_count * sizeof(uint16_t),
                                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (pixels == nullptr) {
        return ESP_ERR_NO_MEM;
    }

    const uint16_t wire_color = static_cast<uint16_t>((test_color << 8) | (test_color >> 8));
    std::fill_n(pixels, pixel_count, wire_color);
    esp_err_t err = ESP_OK;
    for (uint32_t row = 0; row < BSP_DISPLAY_PANEL_HEIGHT && err == ESP_OK; row += rows_per_chunk) {
        const uint32_t row_end = std::min<uint32_t>(row + rows_per_chunk, BSP_DISPLAY_PANEL_HEIGHT);
        err = esp_lcd_panel_draw_bitmap(s_panel, 0, row, BSP_DISPLAY_PANEL_WIDTH, row_end, pixels);
    }
    heap_caps_free(pixels);
    return err;
}

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
    bus_cfg.max_transfer_sz = 4 * 1024;

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
    vendor_config.init_cmds = bsp_display_get_init_commands(&s_init_command_count);
    vendor_config.init_cmds_size = s_init_command_count;

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
        err = esp_lcd_panel_disp_on_off(s_panel, false);
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
        esp_lcd_panel_disp_on_off(s_panel, true);
        esp_lcd_panel_del(s_panel);
        s_panel = nullptr;
        esp_lcd_panel_io_del(s_panel_io);
        s_panel_io = nullptr;
        spi_bus_free(BSP_SPI_HOST);
        s_spi_bus_initialized = false;
        return err;
    }

    err = display_direct_color_test();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Direct QSPI color test failed: %s", esp_err_to_name(err));
        bsp_display_deinit();
        return err;
    }

    const lvgl_port_cfg_t lvgl_config = ESP_LVGL_PORT_INIT_CONFIG();
    err = lvgl_port_init(&lvgl_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port init failed: %s", esp_err_to_name(err));
        bsp_display_deinit();
        return err;
    }

    lvgl_port_display_cfg_t lvgl_display_config = {};
    lvgl_display_config.io_handle = s_panel_io;
    lvgl_display_config.panel_handle = s_panel;
    lvgl_display_config.buffer_size = BSP_DISPLAY_PANEL_WIDTH * BSP_DISPLAY_PANEL_HEIGHT;
    lvgl_display_config.double_buffer = false;
    lvgl_display_config.hres = BSP_DISPLAY_PANEL_WIDTH;
    lvgl_display_config.vres = BSP_DISPLAY_PANEL_HEIGHT;
    lvgl_display_config.color_format = LV_COLOR_FORMAT_RGB565;
    lvgl_display_config.flags.buff_spiram = true;
    lvgl_display_config.flags.full_refresh = true;
    lvgl_display_config.flags.buff_dma = true;
    lvgl_display_config.flags.swap_bytes = true;
    s_lvgl_display = lvgl_port_add_disp(&lvgl_display_config);
    if (s_lvgl_display == nullptr) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        lvgl_port_deinit();
        bsp_display_deinit();
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void bsp_display_print_status(void) {
    if (s_panel == nullptr || s_lvgl_display == nullptr) {
        return;
    }

    char display_info[128];
    char display_bus_info[128];
    char display_protocol_info[128];
    char lvgl_info[128];
    std::snprintf(display_info, sizeof(display_info), "Host=%d, SCLK=%d, D0..D3=%d/%d/%d/%d, CS=%d",
                  BSP_SPI_HOST, BSP_SPI_SCLK_GPIO, BSP_SPI_QSPI_IO0_GPIO, BSP_SPI_QSPI_IO1_GPIO,
                  BSP_SPI_QSPI_IO2_GPIO, BSP_SPI_QSPI_IO3_GPIO, BSP_SPI_CS_GPIO);
    std::snprintf(display_bus_info, sizeof(display_bus_info), "QSPI x4, SPI mode 3, %u MHz",
                  static_cast<unsigned>(BSP_SPI_CLOCK_HZ / 1000000U));
    std::snprintf(display_protocol_info, sizeof(display_protocol_info),
                  "16-bit RGB565, %u-bit commands, %u-bit parameters, %u init commands",
                  32U, 8U, static_cast<unsigned>(s_init_command_count));
    std::snprintf(lvgl_info, sizeof(lvgl_info), "LVGL full refresh, single DMA/PSRAM buffer (%u pixels)",
                  static_cast<unsigned>(BSP_DISPLAY_PANEL_WIDTH * BSP_DISPLAY_PANEL_HEIGHT));
    app::SerialBoxPrinter printer("DISPLAY STATUS");
    printer.add_body_bullet("Controller: AXS15231B initialized", 2U);
    printer.add_body_bullet("Backlight: enabled", 2U);
    printer.add_body_bullet("Panel: " + std::to_string(BSP_DISPLAY_PANEL_WIDTH) + "x" +
                            std::to_string(BSP_DISPLAY_PANEL_HEIGHT) + " pixels", 2U);
    printer.add_body_bullet("Bus: " + std::string(display_bus_info), 2U);
    printer.add_body_bullet("Format: " + std::string(display_protocol_info), 2U);
    printer.add_body_bullet("Pins: " + std::string(display_info), 2U);
    printer.add_body_bullet("LVGL: " + std::string(lvgl_info), 2U);
    printer.print();
}

esp_err_t bsp_display_deinit(void) {
    esp_err_t first_err = bsp_display_set_backlight(false);
    if (s_lvgl_display != nullptr) {
        esp_err_t err = lvgl_port_remove_disp(s_lvgl_display);
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
        s_lvgl_display = nullptr;
        err = lvgl_port_deinit();
        if (first_err == ESP_OK && err != ESP_OK) {
            first_err = err;
        }
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

esp_err_t bsp_display_set_solid_color(uint32_t rgb888) {
    if (s_lvgl_display == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!lvgl_port_lock(0)) {
        return ESP_ERR_TIMEOUT;
    }
    lv_obj_t *screen = lv_display_get_screen_active(s_lvgl_display);
    lv_obj_set_style_bg_color(screen, lv_color_hex(rgb888), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lvgl_port_unlock();
    return ESP_OK;
}

esp_err_t bsp_display_set_rotation(uint16_t rotation) {
    (void)rotation;
    return ESP_OK;
}
