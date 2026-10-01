#pragma once

#include "esp_err.h"

typedef struct {
    char ssid[33];
    char station_mac[18];
    char access_point_mac[18];
    char security[24];
    char ip_address[16];
    char subnet_mask[16];
    char gateway[16];
    char dns_server[16];
    uint8_t channel;
    int8_t rssi_dbm;
} bsp_wifi_status_t;

typedef void (*bsp_wifi_connected_callback_t)(const bsp_wifi_status_t *status, void *context);

esp_err_t bsp_wifi_init(const char *ssid, const char *password,
                        bsp_wifi_connected_callback_t connected_callback, void *context);
void bsp_wifi_print_status(void);