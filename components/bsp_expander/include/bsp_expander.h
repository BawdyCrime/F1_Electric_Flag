#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_EXPANDER_PORT_0 (1U << 0)
#define BSP_EXPANDER_PORT_1 (1U << 1)
#define BSP_EXPANDER_PORT_2 (1U << 2)
#define BSP_EXPANDER_PORT_3 (1U << 3)
#define BSP_EXPANDER_PORT_4 (1U << 4)
#define BSP_EXPANDER_PORT_5 (1U << 5)
#define BSP_EXPANDER_PORT_6 (1U << 6)
#define BSP_EXPANDER_PORT_7 (1U << 7)

/* The vendor board examples identify P1 as LCD reset; other functions are unverified. */
#define BSP_EXPANDER_LCD_RESET BSP_EXPANDER_PORT_1

esp_err_t bsp_expander_init(void);
esp_err_t bsp_expander_set_direction(uint32_t pin_mask, bool output);
esp_err_t bsp_expander_write(uint32_t pin_mask, bool high);
esp_err_t bsp_expander_read(uint32_t pin_mask, uint32_t *level_mask);
esp_err_t bsp_expander_pulse_lcd_reset(void);
esp_err_t bsp_expander_print_state(void);
esp_err_t bsp_expander_deinit(void);

#ifdef __cplusplus
}
#endif