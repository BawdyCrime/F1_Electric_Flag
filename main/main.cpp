#include <cstdio>
#include <string>
#include <vector>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"

#include "bsp_board.h"
#include "bsp_pmic.h"
#include "serial_box_printer.h"

static const char *TAG = "main";

static const char *chip_model_name(uint8_t model) {
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

static void print_board_info(void) {
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

    app::SerialBoxPrinter printer(TAG, "BOARD INFO");
    printer.add_body_line(std::string("S/N: ") + serial);
    printer.add_body_line(std::string("CPU: ") + chip_model_name(chip_info.model) + " (" + std::to_string(chip_info.cores) + " core" + (chip_info.cores > 1 ? "s" : "") + ")");
    printer.add_body_line(std::string("RAM: ") + std::to_string(esp_get_free_heap_size()) + " bytes free heap");
    printer.add_body_line(std::string("ROM: ") + rom_value);
    printer.print();
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting phase-1 BSP validation");

    esp_err_t err = bsp_board_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "bsp_board_init failed: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Board GPIO layer initialized");

    err = bsp_board_i2c_scan();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "I2C scan complete");
    } else {
        ESP_LOGW(TAG, "I2C scan did not detect devices; board power and schematic validation are still required");
    }

    err = bsp_pmic_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "PMIC validation failed: %s", esp_err_to_name(err));
        return;
    }

    uint8_t status1 = 0;
    uint8_t status2 = 0;
    uint8_t chip_id = 0;
    err = bsp_pmic_read_status(&status1, &status2, &chip_id);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "PMIC status: chip_id=0x%02X, status1=0x%02X, status2=0x%02X", chip_id, status1, status2);
    } else {
        ESP_LOGE(TAG, "PMIC status readback failed: %s", esp_err_to_name(err));
    }

    print_board_info();

    ESP_LOGI(TAG, "Phase-1 validation ready for schematic-confirmed PMIC rail sequencing and GPIO checks");
}
