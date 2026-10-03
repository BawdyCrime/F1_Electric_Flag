#include "app_time.h"
#include "board_setup.h"
#include "bsp_board.h"
#include "credentials.h"
#include "f1_types.h"
#include "openf1_client.h"
#include "session_selector.h"
#include "touch_input.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <vector>

static const char *TAG = "main";

// Data-flow control: screen and OpenF1 client never talk to each other.
// The screen reports user actions as requests; main fetches data and hands it to the screen.
enum class request_type_t : uint8_t {
    load_meetings,
    load_sessions,
};

struct request_t {
    request_type_t type;
    uint32_t meeting_key;
};

static QueueHandle_t s_requests = nullptr;
static int s_year = 0;

static void post_request(request_type_t type, uint32_t meeting_key)
{
    const request_t request = {type, meeting_key};
    if (s_requests == nullptr || xQueueSend(s_requests, &request, 0) != pdPASS) {
        ESP_LOGW(TAG, "Request queue full, dropping request");
    }
}

static void on_meeting_selected(uint32_t meeting_key, void *)
{
    post_request(request_type_t::load_sessions, meeting_key);
}

static void on_session_selected(uint32_t meeting_key, uint32_t session_key, void *)
{
    ESP_LOGI(TAG, "Session selected: meeting=%u session=%u",
             static_cast<unsigned>(meeting_key), static_cast<unsigned>(session_key));
}

static void load_meetings()
{
    std::vector<f1::Meeting> meetings;
    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 5 && err != ESP_OK; ++attempt) {
        err = openf1::fetch_meetings(s_year, &meetings);
        if (err != ESP_OK && attempt < 4) {
            vTaskDelay(pdMS_TO_TICKS(4000));
        }
    }
    if (err == ESP_OK) {
        session_selector_show_meetings(meetings);
    } else {
        ESP_LOGE(TAG, "Unable to load race calendar: %s", esp_err_to_name(err));
        session_selector_show_status("SCHEDULE UNAVAILABLE");
    }
}

static void load_sessions(uint32_t meeting_key)
{
    std::vector<f1::Session> sessions;
    esp_err_t err = openf1::fetch_sessions(meeting_key, &sessions);
    if (err == ESP_OK) {
        session_selector_show_sessions(meeting_key, sessions);
    } else {
        ESP_LOGE(TAG, "Unable to load sessions: %s", esp_err_to_name(err));
        session_selector_show_status("SESSIONS UNAVAILABLE");
    }
}

static void data_task(void *)
{
    request_t request = {};
    while (true) {
        if (xQueueReceive(s_requests, &request, portMAX_DELAY) != pdPASS) {
            continue;
        }
        switch (request.type) {
        case request_type_t::load_meetings:
            load_meetings();
            break;
        case request_type_t::load_sessions:
            load_sessions(request.meeting_key);
            break;
        }
    }
}

extern "C" void app_main(void)
{
    bsp_board_print_info();

    esp_err_t err = board_setup_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BSP initialization failed: %s", esp_err_to_name(err));
        return;
    }

    openf1::set_credentials(OPENF1_LOGIN, OPENF1_PASSWORD);
    s_year = app_current_year();

    s_requests = xQueueCreate(4, sizeof(request_t));
    if (s_requests == nullptr) {
        ESP_LOGE(TAG, "Failed to create request queue");
        return;
    }

    session_selector_callbacks_t callbacks;
    callbacks.on_meeting_selected = on_meeting_selected;
    callbacks.on_session_selected = on_session_selected;
    err = session_selector_init(s_year, callbacks);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Session selector init failed: %s", esp_err_to_name(err));
        return;
    }

    if (xTaskCreate(data_task, "data_task", 10240, nullptr, 3, nullptr) != pdPASS ||
        touch_input_start() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create tasks");
        return;
    }
    post_request(request_type_t::load_meetings, 0);
}
