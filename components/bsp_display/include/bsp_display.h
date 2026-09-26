#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_DISPLAY_PANEL_WIDTH  320
#define BSP_DISPLAY_PANEL_HEIGHT 480

esp_err_t bsp_display_init(void);
esp_err_t bsp_display_lvgl_init(void);
esp_err_t bsp_display_deinit(void);
esp_err_t bsp_display_set_backlight(bool enabled);
esp_err_t bsp_display_set_rotation(uint16_t rotation);

#ifdef __cplusplus
}
#endif
