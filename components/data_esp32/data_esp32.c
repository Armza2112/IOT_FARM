#include "esp_wifi.h"
#include <stdio.h>
#include <string.h>
#include "../uuid/uuid.h"
#include "esp_log.h"
char uuidcid[37] = {0};
esp_err_t get_sta_mac(char *mac_str, size_t len)
{
    if (!mac_str || len < 18)
        return ESP_ERR_INVALID_ARG;

    uint8_t mac[6];
    esp_err_t err = esp_wifi_get_mac(WIFI_IF_STA, mac);
    if (err != ESP_OK)
        return err;

    snprintf(mac_str, len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    return ESP_OK;
}

esp_err_t get_sta_rssi(int *rssi, char *rssi_str, size_t str_len)
{
    if (!rssi || !rssi_str || str_len < 2)
        return ESP_ERR_INVALID_ARG;

    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    if (err != ESP_OK)
    {
        *rssi = 0;
        snprintf(rssi_str, str_len, "0");
        return err;
    }

    *rssi = ap_info.rssi;
    snprintf(rssi_str, str_len, "%d", *rssi);
    return ESP_OK;
}
void load_uuid_from_spiffs()
{
    FILE *f = fopen("/spiffs/uuid.txt", "r");
    if (!f)
    {
        ESP_LOGE("UUID", "UUID file not found");
        return;
    }

    fgets(uuidcid, sizeof(uuidcid), f);
    fclose(f);
    uuidcid[strcspn(uuidcid, "\n")] = 0;
    ESP_LOGI("UUID", "Loaded UUID: %s", uuidcid);
}
void load_data_task(){
    initsettingsuuid();
    load_uuid_from_spiffs();
}