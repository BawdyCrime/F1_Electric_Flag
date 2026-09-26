idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bsp_board bsp_display bsp_pmic serial_box_printer spi_flash espressif__esp_lvgl_port lvgl__lvgl
)
