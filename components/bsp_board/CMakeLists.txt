idf_component_register(
    SRCS "bsp_board.cpp" "bsp_board_info.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_common esp_driver_gpio esp_driver_i2c esp_driver_spi esp_psram serial_box_printer spi_flash
)
