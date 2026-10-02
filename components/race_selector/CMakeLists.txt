idf_component_register(
    SRCS "race_selector.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_display display_assets esp_http_client esp-tls esp_lvgl_port cjson lvgl serial_box_printer
)