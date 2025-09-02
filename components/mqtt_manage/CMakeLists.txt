idf_component_register(
    SRCS "mqtt_manage.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_system esp_timer spiffs json driver wifi_manager mqtt
)
