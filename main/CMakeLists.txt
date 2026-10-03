idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES f1_types openf1_client bsp_board bsp_display bsp_pmic bsp_rtc bsp_touch bsp_wifi screen serial_box spi_flash
)
