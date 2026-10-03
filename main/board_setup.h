#pragma once

#include "esp_err.h"

// Brings up all board peripherals (board, RTC, PMIC, expander, display, touch, Wi-Fi).
esp_err_t board_setup_init();
