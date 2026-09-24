#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_PMIC_I2C_ADDR        0x34u
#define BSP_PMIC_CHIP_ID_REG     0x03u
#define BSP_PMIC_STATUS1_REG     0x00u
#define BSP_PMIC_STATUS2_REG     0x01u

esp_err_t bsp_pmic_init(void);
esp_err_t bsp_pmic_deinit(void);
esp_err_t bsp_pmic_read_status(uint8_t *status1, uint8_t *status2, uint8_t *chip_id);
esp_err_t bsp_pmic_enable_rails(bool enable);
esp_err_t bsp_pmic_set_backlight_enable(bool enable);

#ifdef __cplusplus
}
#endif
