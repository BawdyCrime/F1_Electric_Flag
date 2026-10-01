#include "bsp_rtc.h"

#include "bsp_board.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "serial_box_printer.h"

#include <cstdlib>
#include <ctime>
#include <string>
#include <sys/time.h>

namespace {

constexpr uint8_t RTC_SECONDS_REG = 0x04;
constexpr size_t RTC_TIME_REGISTER_COUNT = 7;
constexpr uint8_t RTC_OSCILLATOR_STOP_FLAG = 0x80;
constexpr char RTC_TIME_ZONE[] = "ICT-7";
constexpr char NTP_SERVER[] = "pool.ntp.org";

static const char *TAG = "bsp_rtc";
static i2c_master_dev_handle_t s_rtc_device = nullptr;
static bool s_sntp_initialized = false;
static bool s_rtc_time_valid = false;

uint8_t bcd_to_decimal(uint8_t value)
{
    return static_cast<uint8_t>(((value >> 4U) * 10U) + (value & 0x0FU));
}

uint8_t decimal_to_bcd(uint8_t value)
{
    return static_cast<uint8_t>(((value / 10U) << 4U) | (value % 10U));
}

esp_err_t read_rtc_time(struct tm *calendar, bool *oscillator_stopped)
{
    if (s_rtc_device == nullptr || calendar == nullptr || oscillator_stopped == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t registers[RTC_TIME_REGISTER_COUNT] = {};
    esp_err_t err = i2c_master_transmit_receive(s_rtc_device, &RTC_SECONDS_REG, 1,
                                                 registers, sizeof(registers), 100);
    if (err != ESP_OK) {
        return err;
    }

    *oscillator_stopped = (registers[0] & RTC_OSCILLATOR_STOP_FLAG) != 0;
    calendar->tm_sec = bcd_to_decimal(registers[0] & 0x7FU);
    calendar->tm_min = bcd_to_decimal(registers[1] & 0x7FU);
    calendar->tm_hour = bcd_to_decimal(registers[2] & 0x3FU);
    calendar->tm_mday = bcd_to_decimal(registers[3] & 0x3FU);
    calendar->tm_wday = bcd_to_decimal(registers[4] & 0x07U);
    calendar->tm_mon = bcd_to_decimal(registers[5] & 0x1FU) - 1;
    calendar->tm_year = bcd_to_decimal(registers[6]) + 100;
    calendar->tm_isdst = 0;

    return ESP_OK;
}

esp_err_t write_rtc_time(const struct tm *calendar)
{
    if (s_rtc_device == nullptr || calendar == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t registers[RTC_TIME_REGISTER_COUNT + 1U] = {
        RTC_SECONDS_REG,
        static_cast<uint8_t>(decimal_to_bcd(static_cast<uint8_t>(calendar->tm_sec)) & 0x7FU),
        static_cast<uint8_t>(decimal_to_bcd(static_cast<uint8_t>(calendar->tm_min)) & 0x7FU),
        static_cast<uint8_t>(decimal_to_bcd(static_cast<uint8_t>(calendar->tm_hour)) & 0x3FU),
        static_cast<uint8_t>(decimal_to_bcd(static_cast<uint8_t>(calendar->tm_mday)) & 0x3FU),
        static_cast<uint8_t>(decimal_to_bcd(static_cast<uint8_t>(calendar->tm_wday)) & 0x07U),
        static_cast<uint8_t>(decimal_to_bcd(static_cast<uint8_t>(calendar->tm_mon + 1)) & 0x1FU),
        decimal_to_bcd(static_cast<uint8_t>((calendar->tm_year - 100) % 100)),
    };
    return i2c_master_transmit(s_rtc_device, registers, sizeof(registers), 100);
}

void on_sntp_time_sync(struct timeval *time_value)
{
    (void)time_value;

    struct timeval now = {};
    struct tm local_time = {};
    if (gettimeofday(&now, nullptr) != 0 || localtime_r(&now.tv_sec, &local_time) == nullptr) {
        ESP_LOGE(TAG, "Failed to convert synchronized system time");
        return;
    }

    if (s_rtc_device != nullptr) {
        esp_err_t err = write_rtc_time(&local_time);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update hardware RTC: %s", esp_err_to_name(err));
            return;
        }
        s_rtc_time_valid = true;
    }

    char time_text[32] = {};
    std::strftime(time_text, sizeof(time_text), "%Y-%m-%d %H:%M:%S", &local_time);
    app::SerialBoxPrinter printer("RTC TIME SYNC");
    printer.add_body_bullet("Source: " + std::string(NTP_SERVER));
    printer.add_body_bullet("GMT+7: " + std::string(time_text));
    printer.add_body_bullet(s_rtc_device != nullptr ? "Hardware RTC: updated" : "Hardware RTC: unavailable");
    printer.print();
}

}  // namespace

esp_err_t bsp_rtc_init(void)
{
    setenv("TZ", RTC_TIME_ZONE, 1);
    tzset();

    if (s_rtc_device != nullptr) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t bus = bsp_board_get_i2c_bus_handle();
    if (bus == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const bsp_board_config_t *board = bsp_board_get_config();
    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = board->rtc_i2c_addr,
        .scl_speed_hz = board->i2c_clock_hz,
        .scl_wait_us = 0,
        .flags = {},
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &device_config, &s_rtc_device);
    if (err != ESP_OK) {
        return err;
    }

    struct tm rtc_time = {};
    bool oscillator_stopped = false;
    err = read_rtc_time(&rtc_time, &oscillator_stopped);
    if (err != ESP_OK) {
        return err;
    }

    const bool calendar_valid = rtc_time.tm_mon >= 0 && rtc_time.tm_mon <= 11 &&
                                rtc_time.tm_mday >= 1 && rtc_time.tm_mday <= 31 &&
                                rtc_time.tm_hour <= 23 && rtc_time.tm_min <= 59 &&
                                rtc_time.tm_sec <= 59;
    if (calendar_valid && !oscillator_stopped) {
        const time_t epoch = mktime(&rtc_time);
        if (epoch > 0) {
            struct timeval system_time = {.tv_sec = epoch, .tv_usec = 0};
            err = settimeofday(&system_time, nullptr) == 0 ? ESP_OK : ESP_FAIL;
            s_rtc_time_valid = err == ESP_OK;
            if (err != ESP_OK) {
                return err;
            }
        }
    }

    return ESP_OK;
}

esp_err_t bsp_rtc_start_internet_sync(void)
{
    if (s_sntp_initialized) {
        return ESP_OK;
    }

    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(NTP_SERVER);
    config.sync_cb = on_sntp_time_sync;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err == ESP_OK) {
        s_sntp_initialized = true;
    }
    return err;
}

void bsp_rtc_print_status(void)
{
    app::SerialBoxPrinter printer("REAL-TIME CLOCK");
    printer.add_body_bullet("Device: PCF85063 / " + std::string(s_rtc_device != nullptr ? "0x51" : "unavailable"));
    printer.add_body_bullet("Time zone: GMT+7");
    printer.add_body_bullet(std::string("RTC time: ") + (s_rtc_time_valid ? "loaded" : "awaiting internet sync"));
    printer.print();
}