idf_component_register(
    SRCS "bsp_board.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_gpio esp_driver_i2c
)
