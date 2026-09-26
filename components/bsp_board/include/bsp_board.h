#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The vendor example for this board family maps the shared I2C bus to GPIO8/SDA and GPIO7/SCL,
 * and the panel backlight to GPIO6. This config follows that example while keeping any unconfirmed
 * signals as GPIO_NUM_NC until the schematic is checked.
 */
#define BSP_I2C_PORT       I2C_NUM_0
#define BSP_I2C_SDA_GPIO   GPIO_NUM_8
#define BSP_I2C_SCL_GPIO   GPIO_NUM_7
#define BSP_I2C_CLOCK_HZ   100000u
#define BSP_STATUS_LED_GPIO GPIO_NUM_NC
#define BSP_TOUCH_INT_GPIO GPIO_NUM_NC
#define BSP_TOUCH_RST_GPIO GPIO_NUM_NC
#define BSP_LCD_BL_GPIO    GPIO_NUM_6

/*
 * Verified from the legacy ESP32-S3-Touch-LCD-3.5B board examples.
 * The LCD uses a 4-wire QSPI-style interface with D0..D3 on GPIO1..4,
 * SCLK on GPIO5, chip-select on GPIO12, and backlight on GPIO6.
 */
#define BSP_SPI_HOST          SPI2_HOST
#define BSP_SPI_SCLK_GPIO     GPIO_NUM_5
#define BSP_SPI_MOSI_GPIO     GPIO_NUM_1
#define BSP_SPI_MISO_GPIO     GPIO_NUM_NC
#define BSP_SPI_CS_GPIO       GPIO_NUM_12
#define BSP_SPI_QSPI_IO0_GPIO GPIO_NUM_1
#define BSP_SPI_QSPI_IO1_GPIO GPIO_NUM_2
#define BSP_SPI_QSPI_IO2_GPIO GPIO_NUM_3
#define BSP_SPI_QSPI_IO3_GPIO GPIO_NUM_4
#define BSP_SPI_CLOCK_HZ      40000000u

typedef struct {
    i2c_port_t i2c_port;
    gpio_num_t i2c_sda_gpio;
    gpio_num_t i2c_scl_gpio;
    uint32_t i2c_clock_hz;
    gpio_num_t status_led_gpio;
    gpio_num_t touch_int_gpio;
    gpio_num_t touch_rst_gpio;
    gpio_num_t lcd_bl_gpio;

    spi_host_device_t spi_host;
    gpio_num_t spi_sclk_gpio;
    gpio_num_t spi_mosi_gpio;
    gpio_num_t spi_miso_gpio;
    gpio_num_t spi_cs_gpio;
    gpio_num_t spi_qspi_io0_gpio;
    gpio_num_t spi_qspi_io1_gpio;
    gpio_num_t spi_qspi_io2_gpio;
    gpio_num_t spi_qspi_io3_gpio;
    uint32_t spi_clock_hz;

    uint8_t pmic_i2c_addr_1;
    uint8_t pmic_i2c_addr_2;
    uint8_t imu_i2c_addr;
    uint8_t rtc_i2c_addr;
} bsp_board_config_t;

const bsp_board_config_t *bsp_board_get_config(void);
i2c_master_bus_handle_t bsp_board_get_i2c_bus_handle(void);
esp_err_t bsp_board_init(void);
esp_err_t bsp_board_i2c_scan(void);
void bsp_board_print_info(void);
void bsp_board_print_peripheral(void);
esp_err_t bsp_board_set_backlight(bool enabled);
esp_err_t bsp_board_set_status_led(bool enabled);

#ifdef __cplusplus
}
#endif
