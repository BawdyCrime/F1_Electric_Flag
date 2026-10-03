#include "board_setup.h"

#include "bsp_board.h"
#include "bsp_display.h"
#include "bsp_expander.h"
#include "bsp_pmic.h"
#include "bsp_rtc.h"
#include "bsp_touch.h"
#include "bsp_wifi.h"
#include "credentials.h"
#include "serial_box.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string>

static const char *TAG = "board_setup";

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

esp_err_t board_setup_init()
{
    esp_err_t err = bsp_board_init();
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
