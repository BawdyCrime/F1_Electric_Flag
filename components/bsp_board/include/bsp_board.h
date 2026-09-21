#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_I2C_PORT       I2C_NUM_0
#define BSP_I2C_SDA_GPIO   GPIO_NUM_43
#define BSP_I2C_SCL_GPIO   GPIO_NUM_44
#define BSP_STATUS_LED_GPIO GPIO_NUM_NC
#define BSP_TOUCH_INT_GPIO GPIO_NUM_38
#define BSP_TOUCH_RST_GPIO GPIO_NUM_21
#define BSP_LCD_BL_GPIO    GPIO_NUM_NC

esp_err_t bsp_board_init(void);
esp_err_t bsp_board_i2c_scan(void);
esp_err_t bsp_board_set_backlight(bool enabled);
esp_err_t bsp_board_set_status_led(bool enabled);

#ifdef __cplusplus
}
#endif
