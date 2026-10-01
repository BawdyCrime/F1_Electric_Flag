idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES bsp_board bsp_display bsp_pmic bsp_rtc bsp_touch bsp_wifi flag_display serial_box_printer spi_flash
)
