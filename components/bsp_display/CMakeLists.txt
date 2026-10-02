idf_component_register(
    SRCS "bsp_display.cpp" "bsp_display_init.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp_board bsp_expander esp_common esp_driver_spi esp_lcd espressif__esp_lcd_axs15231b espressif__esp_lvgl_port serial_box
)
