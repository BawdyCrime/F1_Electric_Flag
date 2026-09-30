idf_component_register(
    SRCS "flag_display.cpp" "dot_matrix.cpp" "matrix_patterns.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_display lvgl espressif__esp_lvgl_port
    EMBED_FILES "f1_logo_192x48.rgb565"
)
