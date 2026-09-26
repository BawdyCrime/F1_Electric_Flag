#include "bsp_touch.h"

#include "serial_box_printer.h"

esp_err_t bsp_touch_init(void) {
    app::SerialBoxPrinter printer("TOUCH STATUS");
    printer.add_body_bullet("Initialization staged pending touch IC and I2C address confirmation from schematic", 2U);
    printer.print();
    return ESP_OK;
}

esp_err_t bsp_touch_deinit(void) {
    app::SerialBoxPrinter printer("TOUCH STATUS");
    printer.add_body_bullet("Deinitialize placeholder called", 2U);
    printer.print();
    return ESP_OK;
}

esp_err_t bsp_touch_read(bsp_touch_point_t *point) {
    if (point == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }

    point->x = 0;
    point->y = 0;
    point->pressed = false;
    app::SerialBoxPrinter printer("TOUCH STATUS");
    printer.add_body_bullet("Read placeholder; no hardware touch IC validated yet", 2U);
    printer.print();
    return ESP_OK;
}

esp_err_t bsp_touch_reset(void) {
    app::SerialBoxPrinter printer("TOUCH STATUS");
    printer.add_body_bullet("Reset placeholder called", 2U);
    printer.print();
    return ESP_OK;
}
