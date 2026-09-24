#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

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
#define BSP_STATUS_LED_GPIO GPIO_NUM_NC
#define BSP_TOUCH_INT_GPIO GPIO_NUM_NC
#define BSP_TOUCH_RST_GPIO GPIO_NUM_NC
#define BSP_LCD_BL_GPIO    GPIO_NUM_6

i2c_master_bus_handle_t bsp_board_get_i2c_bus_handle(void);
esp_err_t bsp_board_init(void);
esp_err_t bsp_board_i2c_scan(void);
esp_err_t bsp_board_set_backlight(bool enabled);
esp_err_t bsp_board_set_status_led(bool enabled);

#ifdef __cplusplus
}
#endif
