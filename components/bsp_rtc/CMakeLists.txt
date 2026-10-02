idf_component_register(
    SRCS "bsp_rtc.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_board esp_driver_i2c esp_netif serial_box
)