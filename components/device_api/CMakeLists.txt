idf_component_register(
    SRCS "device_api.c"
    INCLUDE_DIRS "."   
    REQUIRES esp_http_client esp_wifi json spiffs
)
