#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t race_selector_init(void);
void race_selector_handle_touch(bool pressed, int x, int y);
bool race_selector_get_selected_session(uint32_t *meeting_key, uint32_t *session_key);

#ifdef __cplusplus
}
#endif