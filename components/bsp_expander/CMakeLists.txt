idf_component_register(
    SRCS "bsp_expander.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_board esp_common esp_driver_i2c espressif__esp_io_expander_tca9554 serial_box_printer
)