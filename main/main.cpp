#include "esp_log.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_expander.h"
#include "bsp_pmic.h"
#include "bsp_rtc.h"
#include "bsp_touch.h"
#include "bsp_wifi.h"
#include "session_selector.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_box.h"
#include "freertos/queue.h"
#include "openf1_client.h"

#if __has_include("wifi_credentials.local.h")
#include "wifi_credentials.local.h"
#else
#define F1_WIFI_SSID ""
#define F1_WIFI_PASSWORD ""
#endif

#if __has_include("openf1_credentials.local.h")
#include "openf1_credentials.local.h"
#else
#define OPENF1_LOGIN ""
#define OPENF1_PASSWORD ""
#endif

#include <cstdio>
#include <ctime>
#include <cstdlib>
#include <string>
#include <vector>

static const char *TAG = "main";

static void wifi_connected_callback(const bsp_wifi_status_t *status, void *context)
{
    (void)context;

    app::SerialBoxPrinter printer("WI-FI");
    printer.add_body_bullet("Connected");
    printer.add_body_bullet("SSID: " + std::string(status->ssid));
    printer.add_body_bullet("Device MAC: " + std::string(status->station_mac));
    printer.add_body_bullet("Access point: " + std::string(status->access_point_mac));
    printer.add_body_bullet("Band / channel: 2.4 GHz / " + std::to_string(status->channel));
    printer.add_body_bullet("Signal: " + std::to_string(status->rssi_dbm) + " dBm");
    printer.add_body_bullet("Security: " + std::string(status->security));
    printer.add_body_bullet("IP: " + std::string(status->ip_address));
    printer.add_body_bullet("Subnet: " + std::string(status->subnet_mask));
    printer.add_body_bullet("Gateway: " + std::string(status->gateway));
    printer.add_body_bullet("DNS: " + std::string(status->dns_server));
    printer.print();

    esp_err_t err = bsp_rtc_start_internet_sync();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RTC internet time sync start failed: %s", esp_err_to_name(err));
    }
}

static int current_year()
{
    time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm calendar = {};
        gmtime_r(&now, &calendar);
        return calendar.tm_year + 1900;
    }
    char build_year[5] = {};
    std::snprintf(build_year, sizeof(build_year), "%.4s", __DATE__ + 7);
    return std::atoi(build_year);
}

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

static void data_task(void *arg)
{
    (void)arg;
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

static void touch_task(void *arg)
{
    (void)arg;

    while (true) {
        bsp_touch_point_t point = {};
        esp_err_t err = bsp_touch_read(&point);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Touch read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        session_selector_handle_touch(point.pressed, point.x, point.y);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

static esp_err_t bsp_init(void)
{
    esp_err_t err = ESP_OK;
    
    err = bsp_board_init();
    if (err == ESP_OK) {
        bsp_board_print_status();
    } else {
        ESP_LOGE(TAG, "Board init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_rtc_init();
    if (err == ESP_OK) {
        bsp_rtc_print_status();
    } else {
        ESP_LOGE(TAG, "RTC init failed: %s", esp_err_to_name(err));
    }

    err = bsp_pmic_init();
    if (err == ESP_OK) {
        bsp_pmic_print_status();
    } else {
        ESP_LOGE(TAG, "PMIC init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_expander_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Expander init/status failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_expander_pulse_lcd_reset();
    if (err == ESP_OK) {
        bsp_expander_print_status();
    } else {
        ESP_LOGE(TAG, "Expander pulse LCD reset failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_display_init();
    if (err == ESP_OK) {
        bsp_display_print_status();
    } else {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_touch_init(BSP_DISPLAY_PANEL_WIDTH, BSP_DISPLAY_PANEL_HEIGHT);
    if (err == ESP_OK) {
        bsp_touch_print_status();
    } else {
        ESP_LOGE(TAG, "Touch init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = bsp_wifi_init(F1_WIFI_SSID, F1_WIFI_PASSWORD, wifi_connected_callback, nullptr);
    if (err == ESP_OK) {
        bsp_wifi_print_status();
    } else {
        ESP_LOGE(TAG, "Wi-Fi init failed: %s", esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));
    return ESP_OK;
}

extern "C" void app_main(void)
{
    esp_err_t err = ESP_OK;

    // Print board information
    bsp_board_print_info();

    // Initialize the board support package (BSP)
    err = bsp_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BSP initialization failed: %s", esp_err_to_name(err));
        return;
    }

    openf1::set_credentials(OPENF1_LOGIN, OPENF1_PASSWORD);
    s_year = current_year();

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
        xTaskCreate(touch_task, "touch_task", 4096, nullptr, 3, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create tasks");
        return;
    }
    post_request(request_type_t::load_meetings, 0);
}
