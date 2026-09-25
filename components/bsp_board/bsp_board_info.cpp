#include "bsp_board.h"

#include <cstdio>
#include <string>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_psram.h"
#include "esp_system.h"

#include "serial_box_printer.h"

namespace {

const char *chip_model_name(uint8_t model) {
    switch (model) {
        case CHIP_ESP32:
            return "ESP32";
        case CHIP_ESP32S2:
            return "ESP32-S2";
        case CHIP_ESP32S3:
            return "ESP32-S3";
        case CHIP_ESP32C3:
            return "ESP32-C3";
        case CHIP_ESP32C2:
            return "ESP32-C2";
        case CHIP_ESP32C6:
            return "ESP32-C6";
        case CHIP_ESP32H2:
            return "ESP32-H2";
        case CHIP_ESP32P4:
            return "ESP32-P4";
        default:
            return "ESP32 family";
    }
}

std::string reset_reason_string(void) {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:
            return "POWERON";
        case ESP_RST_EXT:
            return "EXT";
        case ESP_RST_SW:
            return "SW";
        case ESP_RST_PANIC:
            return "PANIC";
        case ESP_RST_INT_WDT:
            return "INT_WDT";
        case ESP_RST_TASK_WDT:
            return "TASK_WDT";
        case ESP_RST_WDT:
            return "WDT";
        case ESP_RST_DEEPSLEEP:
            return "DEEPSLEEP";
        case ESP_RST_BROWNOUT:
            return "BROWNOUT";
        case ESP_RST_SDIO:
            return "SDIO";
        default:
            return "UNKNOWN";
    }
}

std::string format_mb_value(uint32_t bytes) {
    const double mb = static_cast<double>(bytes) / 1024.0 / 1024.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f MB", mb);
    return buf;
}

std::string format_mb_free(size_t bytes) {
    const double mb = static_cast<double>(bytes) / 1024.0 / 1024.0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f MB free", mb);
    return buf;
}

}  // namespace

void bsp_board_print_info(void) {
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint8_t mac[6] = {0};
    char serial[32] = "N/A";
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
        std::snprintf(serial, sizeof(serial), "%02X:%02X:%02X:%02X:%02X:%02X",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    uint32_t flash_size_bytes = 0;
    std::string flash_value = "unknown";
    if (esp_flash_get_size(NULL, &flash_size_bytes) == ESP_OK) {
        flash_value = format_mb_value(flash_size_bytes);
    }

    size_t psram_size = esp_psram_get_size();
    std::string psram_value = "N/A";
    if (psram_size > 0) {
        psram_value = format_mb_value(static_cast<uint32_t>(psram_size));
    }

    const bool wifi_available = (chip_info.features & CHIP_FEATURE_WIFI_BGN) != 0;
    const bool bt_available = (chip_info.features & CHIP_FEATURE_BT) != 0;
    const bool ble_available = (chip_info.features & CHIP_FEATURE_BLE) != 0;

    app::SerialBoxPrinter printer("ESP32-S3 SYSTEM INFORMATION");
    printer.add_blank_body();
    printer.add_body_line("DEVICE");
    printer.add_body_bullet("CPU: " + std::string(chip_model_name(chip_info.model)) + " • " + std::to_string(chip_info.cores) + " Cores", 2U);
    printer.add_body_bullet("Revision: v" + std::to_string(chip_info.revision), 2U);
    printer.add_body_bullet("Clock: " + std::to_string(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) + " MHz", 2U);
    printer.add_blank_body();
    printer.add_body_line("MEMORY");
    printer.add_body_bullet("Flash: " + flash_value, 2U);
    printer.add_body_bullet("PSRAM: " + psram_value, 2U);
    printer.add_body_bullet("Heap: " + format_mb_free(esp_get_free_heap_size()), 2U);
    printer.add_body_bullet("Min Heap: " + format_mb_free(esp_get_minimum_free_heap_size()), 2U);
    printer.add_blank_body();
    printer.add_body_line("CONNECTIVITY");
    printer.add_body_bullet("Wi-Fi: " + std::string(wifi_available ? "Available" : "Unavailable"), 2U);
    printer.add_body_bullet(std::string(bt_available || ble_available ? "Bluetooth / BLE" : "Bluetooth / BLE: N/A"), 2U);
    printer.add_blank_body();
    printer.add_body_line("SYSTEM");
    printer.add_body_bullet("Reset: " + reset_reason_string(), 2U);
    printer.add_blank_body();
    printer.add_body_line("S/N: " + std::string(serial));
    printer.print();
}
