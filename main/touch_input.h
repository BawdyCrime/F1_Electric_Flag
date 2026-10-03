#pragma once

#include "esp_err.h"

// Polls the touch controller and forwards presses to the session selector.
// Presses in the header area (left/right half) call replay_adjust_offset(-/+ 10 s) once replay is running.
esp_err_t touch_input_start();

// Implemented in main.cpp.
void replay_adjust_offset(int delta_s);
