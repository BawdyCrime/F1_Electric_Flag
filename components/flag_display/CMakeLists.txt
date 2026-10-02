idf_component_register(
    SRCS "flag_display.cpp" "flag_header.cpp" "flag_interval.cpp" "flag_panel.cpp" "dot_matrix.cpp" "matrix_patterns.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bsp_display lvgl espressif__esp_lvgl_port
    EMBED_FILES
        "bitmap/f1_logo_192x48.rgb565"
        "bitmap/constructors/team_logo_rbr.rgb565"
        "bitmap/constructors/team_logo_mcl.rgb565"
        "bitmap/constructors/team_logo_fer.rgb565"
        "bitmap/constructors/team_logo_mer.rgb565"
        "bitmap/constructors/team_logo_ast.rgb565"
        "bitmap/constructors/team_logo_alp.rgb565"
        "bitmap/constructors/team_logo_wil.rgb565"
        "bitmap/constructors/team_logo_rb.rgb565"
        "bitmap/constructors/team_logo_aud.rgb565"
        "bitmap/constructors/team_logo_has.rgb565"
        "bitmap/constructors/team_logo_cad.rgb565"
)
