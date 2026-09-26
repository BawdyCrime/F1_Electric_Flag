#include "bsp_imu.h"

#include "serial_box_printer.h"

esp_err_t bsp_imu_init(void) {
    app::SerialBoxPrinter printer("IMU STATUS");
    printer.add_body_line("Initialization staged pending chip identification and schematic register-map validation");
    printer.print();
    return ESP_OK;
}

esp_err_t bsp_imu_deinit(void) {
    app::SerialBoxPrinter printer("IMU STATUS");
    printer.add_body_line("Deinitialize placeholder called");
    printer.print();
    return ESP_OK;
}

esp_err_t bsp_imu_read(bsp_imu_data_t *data) {
    if (data == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    data->ax = 0.0f;
    data->ay = 0.0f;
    data->az = 0.0f;
    data->gx = 0.0f;
    data->gy = 0.0f;
    data->gz = 0.0f;
    app::SerialBoxPrinter printer("IMU STATUS");
    printer.add_body_line("Read placeholder; sensor not validated on hardware");
    printer.print();
    return ESP_OK;
}
