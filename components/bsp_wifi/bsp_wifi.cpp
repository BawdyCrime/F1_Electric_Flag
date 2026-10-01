#include "bsp_wifi.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "serial_box_printer.h"

#include <cstdio>
#include <cstring>

static const char *TAG = "bsp_wifi";
static bsp_wifi_connected_callback_t s_connected_callback = nullptr;
static void *s_callback_context = nullptr;
static esp_netif_t *s_station_netif = nullptr;
static bool s_wifi_initialized = false;
static char s_configured_ssid[33] = {};

static void format_mac(const uint8_t mac[6], char output[18])
{
    std::snprintf(output, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void format_ip(const esp_ip4_addr_t *ip, char output[16])
{
    std::snprintf(output, 16, IPSTR, IP2STR(ip));
}

static const char *security_name(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "Open";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA-PSK";
    case WIFI_AUTH_WPA2_PSK: return "WPA2-PSK";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2-PSK";
    case WIFI_AUTH_ENTERPRISE: return "Enterprise";
    case WIFI_AUTH_WPA3_PSK: return "WPA3-PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3-PSK";
    case WIFI_AUTH_WAPI_PSK: return "WAPI-PSK";
    case WIFI_AUTH_OWE: return "OWE";
    case WIFI_AUTH_WPA3_ENT_192: return "WPA3-Enterprise-192";
    case WIFI_AUTH_DPP: return "DPP";
    case WIFI_AUTH_WPA3_ENTERPRISE: return "WPA3-Enterprise";
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "WPA2/WPA3-Enterprise";
    case WIFI_AUTH_WPA_ENTERPRISE: return "WPA-Enterprise";
    default: return "Unknown";
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT &&
        (event_id == WIFI_EVENT_STA_START || event_id == WIFI_EVENT_STA_DISCONNECTED)) {
        esp_err_t err = esp_wifi_connect();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Wi-Fi connect request failed: %s", esp_err_to_name(err));
        }
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    (void)arg;

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP && s_connected_callback != nullptr) {
        const auto *event = static_cast<const ip_event_got_ip_t *>(event_data);
        bsp_wifi_status_t status = {};
        wifi_ap_record_t access_point = {};
        uint8_t station_mac[6] = {};

        if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
            std::memcpy(status.ssid, access_point.ssid, sizeof(status.ssid) - 1U);
            format_mac(access_point.bssid, status.access_point_mac);
            std::snprintf(status.security, sizeof(status.security), "%s", security_name(access_point.authmode));
            status.channel = access_point.primary;
            status.rssi_dbm = access_point.rssi;
        }
        if (esp_wifi_get_mac(WIFI_IF_STA, station_mac) == ESP_OK) {
            format_mac(station_mac, status.station_mac);
        }

        format_ip(&event->ip_info.ip, status.ip_address);
        format_ip(&event->ip_info.netmask, status.subnet_mask);
        format_ip(&event->ip_info.gw, status.gateway);

        esp_netif_dns_info_t dns_info = {};
        if (esp_netif_get_dns_info(s_station_netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK &&
            dns_info.ip.type == ESP_IPADDR_TYPE_V4) {
            format_ip(&dns_info.ip.u_addr.ip4, status.dns_server);
        }

        s_connected_callback(&status, s_callback_context);
    }
}

esp_err_t bsp_wifi_init(const char *ssid, const char *password,
                        bsp_wifi_connected_callback_t connected_callback, void *context)
{
    if (s_wifi_initialized) {
        return ESP_OK;
    }
    if (ssid == nullptr || password == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    const size_t ssid_length = std::strlen(ssid);
    const size_t password_length = std::strlen(password);
    if (ssid_length == 0 || ssid_length > 32 ||
        (password_length != 0 && (password_length < 8 || password_length > 63))) {
        ESP_LOGE(TAG, "Invalid Wi-Fi credentials");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = nvs_flash_init();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    s_station_netif = esp_netif_create_default_wifi_sta();
    if (s_station_netif == nullptr) {
        return ESP_FAIL;
    }

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }

    s_connected_callback = connected_callback;
    s_callback_context = context;

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, nullptr);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, nullptr);
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {};
    std::memcpy(wifi_config.sta.ssid, ssid, ssid_length);
    std::memcpy(wifi_config.sta.password, password, password_length);

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) {
        return err;
    }
    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err == ESP_OK) {
        std::memcpy(s_configured_ssid, ssid, ssid_length);
        s_configured_ssid[ssid_length] = '\0';
        s_wifi_initialized = true;
    }
    return err;
}

void bsp_wifi_print_status(void)
{
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "Cannot print Wi-Fi status before initialization");
        return;
    }

    app::SerialBoxPrinter printer("WI-FI STATUS");
    printer.add_body_bullet("Station: initialized", 2U);
    printer.add_body_bullet("SSID: " + std::string(s_configured_ssid), 2U);
    printer.add_body_bullet("Connection: waiting for IP event", 2U);
    printer.print();
}