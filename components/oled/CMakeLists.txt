idf_component_register(SRCS "oled_manage.c" "image_hex.c"
                      INCLUDE_DIRS "."
                      REQUIRES driver i2cdev u8g2 freertos data_esp32 u8g2-hal-esp-idf QRCode esp_wifi button_manage)
