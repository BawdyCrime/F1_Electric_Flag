idf_component_register(
    SRCS "openf1_client.cpp"
    INCLUDE_DIRS "include"
    REQUIRES f1_types esp_http_client esp-tls cjson
)
