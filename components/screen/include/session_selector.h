#pragma once

#include "esp_err.h"
#include "f1_types.h"

#include <cstdint>
#include <vector>

// Pure view: renders data it is given and reports user actions through callbacks.
// Callbacks run on the touch task (not under the LVGL lock) and must not block.
struct session_selector_callbacks_t {
    void (*on_meeting_selected)(uint32_t meeting_key, void *context) = nullptr;
    void (*on_session_selected)(uint32_t meeting_key, uint32_t session_key, void *context) = nullptr;
    void (*on_back)(void *context) = nullptr;
    void *context = nullptr;
};

esp_err_t session_selector_init(int year, const session_selector_callbacks_t &callbacks);
void session_selector_handle_touch(bool pressed, int x, int y);
void session_selector_show_meetings(const std::vector<f1::Meeting> &meetings);
void session_selector_show_sessions(uint32_t meeting_key, const std::vector<f1::Session> &sessions);
void session_selector_show_status(const char *text);
bool session_selector_get_selected_session(uint32_t *meeting_key, uint32_t *session_key);
// While inactive (another screen is shown) touches are ignored by the selector.
void session_selector_set_active(bool active);
