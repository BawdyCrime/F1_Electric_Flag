idf_component_register(
    SRCS "bsp_touch.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_board esp_common esp_driver_i2c esp_lcd espressif__esp_lcd_axs15231b espressif__esp_lcd_touch serial_box
)
