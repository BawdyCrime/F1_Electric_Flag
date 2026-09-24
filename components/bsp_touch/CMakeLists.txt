idf_component_register(
    SRCS "bsp_touch.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_i2c
)
