idf_component_register(
    SRCS "bsp_pmic.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_board esp_common esp_driver_i2c serial_box_printer
)
