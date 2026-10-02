set(F1_LOGO_IMAGE "${CMAKE_CURRENT_LIST_DIR}/../flag_display/bitmap/f1_logo_192x48.rgb565")

idf_component_register(
    SRCS "display_logo.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    EMBED_FILES "${F1_LOGO_IMAGE}"
)