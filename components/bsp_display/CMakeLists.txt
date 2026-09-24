idf_component_register(
    SRCS "bsp_display.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_gpio
)
