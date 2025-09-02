idf_component_register(
    SRCS "web_server.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES 
        esp_wifi
        esp_event
        nvs_flash
        esp_netif
        esp_http_server
        spiffs
)
