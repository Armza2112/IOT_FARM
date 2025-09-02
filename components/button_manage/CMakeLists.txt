idf_component_register(
    SRCS "button_manage.c"
    INCLUDE_DIRS "."
    REQUIRES freertos driver esp_timer esp_wifi esp_http_server
)
