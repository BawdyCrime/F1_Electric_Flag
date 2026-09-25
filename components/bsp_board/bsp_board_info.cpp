#include "bsp_board.h"

#include <cstdio>
#include <string>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
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
    std::string rom_value = "unknown";
    if (esp_flash_get_size(NULL, &flash_size_bytes) == ESP_OK) {
        char rom_buf[32];
        std::snprintf(rom_buf, sizeof(rom_buf), "%u bytes", static_cast<unsigned>(flash_size_bytes));
        rom_value = rom_buf;
    }

    app::SerialBoxPrinter printer("BOARD INFO");
    printer.add_body_line(std::string("S/N: ") + serial);
    printer.add_body_line(std::string("CPU: ") + chip_model_name(chip_info.model) + " (" + std::to_string(chip_info.cores) + " core" + (chip_info.cores > 1 ? "s" : "") + ")");
    printer.add_body_line(std::string("RAM: ") + std::to_string(esp_get_free_heap_size()) + " bytes free heap");
    printer.add_body_line(std::string("ROM: ") + rom_value);
    printer.print();
}
