#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x;
    int y;
    bool pressed;
} bsp_touch_point_t;

esp_err_t bsp_touch_init(void);
esp_err_t bsp_touch_deinit(void);
esp_err_t bsp_touch_read(bsp_touch_point_t *point);
esp_err_t bsp_touch_reset(void);

#ifdef __cplusplus
}
#endif
