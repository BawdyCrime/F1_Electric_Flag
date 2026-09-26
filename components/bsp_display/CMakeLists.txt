idf_component_register(
    SRCS "bsp_display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_board esp_common esp_driver_spi esp_lcd espressif__esp_lcd_axs15231b espressif__esp_lvgl_port lvgl__lvgl serial_box_printer
)
