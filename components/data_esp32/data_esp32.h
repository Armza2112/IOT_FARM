#ifndef ESP_WIFI_INFO_H
#define ESP_WIFI_INFO_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief อ่าน MAC address ของ Wi-Fi STA
 *
 * @param mac_str buffer ขนาดอย่างน้อย 18 bytes
 * @param len ความยาว buffer
 * @return esp_err_t ESP_OK ถ้าสำเร็จ
 */
esp_err_t get_sta_mac(char *mac_str, size_t len);

/**
 * @brief อ่าน RSSI ของ Wi-Fi STA
 *
 * @param rssi pointer สำหรับเก็บค่า RSSI (dBm)
 * @return esp_err_t ESP_OK ถ้าสำเร็จ
 */
esp_err_t get_sta_rssi(int *rssi, char *rssi_str, size_t str_len);

#endif // ESP_WIFI_INFO_H
