idf_component_register(
    SRCS "flag_display.cpp" "dot_matrix.cpp" "safety_car_matrix.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_display lvgl espressif__esp_lvgl_port
)
