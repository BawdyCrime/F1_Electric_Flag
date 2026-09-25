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

std::string chip_features_string(const esp_chip_info_t &chip_info) {
    std::string features;
    auto add_feature = [&](const char *name) {
        if (!features.empty()) {
            features += ", ";
        }
        features += name;
    };

    if (chip_info.features & CHIP_FEATURE_WIFI_BGN) {
        add_feature("WiFi");
    }
    if (chip_info.features & CHIP_FEATURE_BT) {
        add_feature("BT");
    }
    if (chip_info.features & CHIP_FEATURE_BLE) {
        add_feature("BLE");
    }
    if (chip_info.features & CHIP_FEATURE_EMB_FLASH) {
        add_feature("embedded flash");
    }
    if (chip_info.features & CHIP_FEATURE_EMB_PSRAM) {
        add_feature("embedded PSRAM");
    }
    if (chip_info.features & CHIP_FEATURE_IEEE802154) {
        add_feature("IEEE802.15.4");
    }
    if (features.empty()) {
        features = "none";
    }
    return features;
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
        char flash_buf[32];
        std::snprintf(flash_buf, sizeof(flash_buf), "%u bytes", static_cast<unsigned>(flash_size_bytes));
        flash_value = flash_buf;
    }

    size_t psram_size = esp_psram_get_size();
    std::string psram_value = "not present";
    if (psram_size > 0) {
        char psram_buf[32];
        std::snprintf(psram_buf, sizeof(psram_buf), "%zu bytes", psram_size);
        psram_value = psram_buf;
    }

    app::SerialBoxPrinter printer("BOARD INFO");
    printer.add_body_line(std::string("S/N: ") + serial);
    printer.add_body_line(std::string("CPU: ") + chip_model_name(chip_info.model) + " (" + std::to_string(chip_info.cores) + " core" + (chip_info.cores > 1 ? "s" : "") + ")");
    printer.add_body_line(std::string("Rev: ") + std::to_string(chip_info.revision));
    printer.add_body_line(std::string("Features: ") + chip_features_string(chip_info));
    printer.add_body_line(std::string("CPU clock: ") + std::to_string(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ) + " MHz");
    printer.add_body_line(std::string("Flash: ") + flash_value);
    printer.add_body_line(std::string("PSRAM: ") + psram_value);
    printer.add_body_line(std::string("Reset reason: ") + reset_reason_string());
    printer.add_body_line(std::string("Heap free: ") + std::to_string(esp_get_free_heap_size()) + " bytes");
    printer.add_body_line(std::string("Heap min free: ") + std::to_string(esp_get_minimum_free_heap_size()) + " bytes");
    printer.print();
}
