idf_component_register(
    SRCS "bsp_display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_board esp_common esp_driver_gpio esp_driver_spi
)
