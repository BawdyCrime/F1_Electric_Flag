#pragma once

#include "esp_err.h"

// Polls the touch controller and forwards presses to the session selector.
esp_err_t touch_input_start();
