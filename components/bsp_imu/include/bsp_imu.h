#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float ax;
    float ay;
    float az;
    float gx;
    float gy;
    float gz;
} bsp_imu_data_t;

esp_err_t bsp_imu_init(void);
esp_err_t bsp_imu_deinit(void);
esp_err_t bsp_imu_read(bsp_imu_data_t *data);

#ifdef __cplusplus
}
#endif
