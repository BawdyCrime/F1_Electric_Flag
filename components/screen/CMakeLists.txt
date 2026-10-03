idf_component_register(
    SRCS
        "screen_header/screen_header.cpp"
        "flag_display.cpp"
        "session_selector.cpp"
        "flag_display/flag_panel.cpp"
        "flag_display/dot_matrix.cpp"
        "flag_display/matrix_patterns.cpp"
        "event_timing/event_timing.cpp"
    INCLUDE_DIRS "include" "event_timing" "flag_display" "screen_header"
    REQUIRES bsp_display f1_types espressif__esp_lvgl_port lvgl serial_box
    EMBED_FILES
        "bitmap/f1_logo/f1_logo_192x48.rgb565"
        "bitmap/team_logo/team_logo_rbr.rgb565"
        "bitmap/team_logo/team_logo_mcl.rgb565"
        "bitmap/team_logo/team_logo_fer.rgb565"
        "bitmap/team_logo/team_logo_mer.rgb565"
        "bitmap/team_logo/team_logo_ast.rgb565"
        "bitmap/team_logo/team_logo_alp.rgb565"
        "bitmap/team_logo/team_logo_wil.rgb565"
        "bitmap/team_logo/team_logo_rb.rgb565"
        "bitmap/team_logo/team_logo_aud.rgb565"
        "bitmap/team_logo/team_logo_has.rgb565"
        "bitmap/team_logo/team_logo_cad.rgb565"
)
