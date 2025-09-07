#ifndef ESP_WIFI_INFO_H
#define ESP_WIFI_INFO_H

#include "esp_err.h"
#include <stdint.h>

/**
 * @brief
 *
 * @param mac_str
 * @param len
 * @return
 */
esp_err_t get_sta_mac(char *mac_str, size_t len);

/**
 * @brief
 *
 * @param rssi
 * @return
 */
esp_err_t get_sta_rssi(int *rssi, char *rssi_str, size_t str_len);
void load_uuid_from_spiffs();
void load_data_task();
extern char uuidcid[37];
#endif // ESP_WIFI_INFO_H
