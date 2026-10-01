#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t bsp_rtc_init(void);
esp_err_t bsp_rtc_start_internet_sync(void);
void bsp_rtc_print_status(void);

#ifdef __cplusplus
}
#endif