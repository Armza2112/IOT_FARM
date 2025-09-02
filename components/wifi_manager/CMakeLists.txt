idf_component_register(
    SRCS "wifi_manager.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_wifi
        esp_event
        nvs_flash
        esp_netif
        esp_http_server
        spiffs
)
