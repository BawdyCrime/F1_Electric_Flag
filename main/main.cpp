#include "esp_log.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_expander.h"
#include "bsp_pmic.h"
#include "bsp_touch.h"
#include "bsp_wifi.h"
#include "flag_display.h"
#include "esp_netif_ip_addr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_box_printer.h"

#if __has_include("wifi_credentials.local.h")
#include "wifi_credentials.local.h"
#else
#define F1_WIFI_SSID ""
#define F1_WIFI_PASSWORD ""
#endif

#include <cstdio>
#include <string>

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
}

struct flag_stage_t {
    flag_screen_t screen;
    const char *name;
    esp_err_t (*show)(void);
};

// Demo grid for the 2026 season; clipped by flag_display to however many rows fit on screen.
static const flag_interval_row_t DEMO_INTERVAL_ROWS[] = {
    {1, FLAG_TEAM_RED_BULL_RACING, "VER", "Interval", 'M'},
    {2, FLAG_TEAM_MCLAREN, "NOR", "+1.842", 'M'},
    {3, FLAG_TEAM_FERRARI, "LEC", "+4.391", 'H'},
    {4, FLAG_TEAM_MERCEDES, "RUS", "+6.204", 'H'},
    {5, FLAG_TEAM_ASTON_MARTIN, "ALO", "+9.873", 'M'},
    {6, FLAG_TEAM_ALPINE, "GAS", "+12.55", 'S'},
    {7, FLAG_TEAM_WILLIAMS, "ALB", "+15.02", 'H'},
    {8, FLAG_TEAM_RACING_BULLS, "TSU", "+18.44", 'M'},
    {9, FLAG_TEAM_AUDI, "BOR", "+21.90", 'H'},
    {10, FLAG_TEAM_HAAS, "HUL", "+25.11", 'M'},
    {11, FLAG_TEAM_CADILLAC, "PER", "+28.63", 'H'},
};
static constexpr size_t DEMO_INTERVAL_ROW_COUNT =
    sizeof(DEMO_INTERVAL_ROWS) / sizeof(DEMO_INTERVAL_ROWS[0]);

static const flag_stage_t FLAG_STAGES[] = {
    {FLAG_SCREEN_INTERVAL, "INTERVAL",
     [] { return flag_display_show_interval(15, 52, DEMO_INTERVAL_ROWS, DEMO_INTERVAL_ROW_COUNT); }},
    {FLAG_SCREEN_GREEN, "GREEN FLAG", flag_display_show_green},
    {FLAG_SCREEN_RED, "RED FLAG", flag_display_show_red},
    {FLAG_SCREEN_YELLOW, "YELLOW FLAG", flag_display_show_yellow},
    {FLAG_SCREEN_BLUE, "BLUE FLAG", flag_display_show_blue},
    {FLAG_SCREEN_DOUBLE_YELLOW, "DOUBLE YELLOW FLAG", flag_display_show_double_yellow},
    {FLAG_SCREEN_SAFETY_CAR, "SAFETY CAR", flag_display_show_safety_car},
    {FLAG_SCREEN_VSC, "VSC FLAG", flag_display_show_vsc},
};

static void flag_stage_task(void *arg)
{
    (void)arg;
    constexpr size_t stage_count = sizeof(FLAG_STAGES) / sizeof(FLAG_STAGES[0]);
    bool previous_pressed = false;

    while (true) {
        bsp_touch_point_t point = {};
        esp_err_t err = bsp_touch_read(&point);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Touch read failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        if (point.pressed && !previous_pressed) {
            // Look up the live stage (it may have changed internally, e.g. green auto-revert) before advancing.
            flag_screen_t current = flag_display_get_current_screen();
            size_t current_index = 0;
            for (size_t i = 0; i < stage_count; ++i) {
                if (FLAG_STAGES[i].screen == current) {
                    current_index = i;
                    break;
                }
            }
            size_t stage_index = (current_index + 1U) % stage_count;
            err = FLAG_STAGES[stage_index].show();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Stage change failed: %s", esp_err_to_name(err));
            } else {
                app::SerialBoxPrinter printer("FLAG STAGE");
                printer.add_body_bullet("Position: " + std::to_string(point.x) + ", " + std::to_string(point.y), 2U);
                printer.add_body_bullet("Stage: " + std::string(FLAG_STAGES[stage_index].name), 2U);
                printer.print();
            }
        }
        previous_pressed = point.pressed;
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

    // Initialize the flag display
    err = flag_display_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flag display init failed: %s", esp_err_to_name(err));
        return;
    }

    // Show the initial flag display interval
    err = flag_display_show_interval(15, 52, DEMO_INTERVAL_ROWS, DEMO_INTERVAL_ROW_COUNT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flag display show_interval failed: %s", esp_err_to_name(err));
        return;
    }

    // Create the flag stage task
    BaseType_t task_result = xTaskCreate(flag_stage_task, "flag_stage", 4096, nullptr, 3, nullptr);
    if (task_result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create flag stage task");
        return;
    }
}
