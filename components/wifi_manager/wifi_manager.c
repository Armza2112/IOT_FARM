#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "wifi_manager.h"
#include <string.h>
#include "../web_server/web_server.h"
#include "../time_manage/time_manage.h"
#include "../button_manage/button_manage.h"
#include "../mqtt_manage/mqtt_manage.h"

#define WIFI_SSID "ESP32"
#define WIFI_PASS "12345678"
#define MAXIMUM_AP 20
#define HISTORY_SIZE 100
#define MAX_STA_CONN 4
#define WIFI_CHANNEL 1
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1
#define WIFI_DISCONNECTED_BIT BIT1

void wifi_connect(const char *ssid, const char *password);
void wifi_scan_task(void *pvParameters);
void wifi_reconnect_task();

wifi_ap_record_t scanned_aps[MAXIMUM_AP];
int16_t scanned_ap_count = 0;
TaskHandle_t main_task_handle = NULL;
EventGroupHandle_t wifi_event_group;

bool user_disconnect = false; // var check state dis on web
bool wrong_password = false;  // var check state wrong password on web

static const char *TAG_SCAN = "wifi_scan";
static const char *TAG_AP = "wifi_softap_web";
static const char *TAG_RE = "wifi_manager";

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_AP_STACONNECTED:
        {
            wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "station " MACSTR " join, AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED:
        {
            wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *)event_data;
            ESP_LOGI(TAG_AP, "station " MACSTR " leave, AID=%d",
                     MAC2STR(event->mac), event->aid);
            break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t *disc = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGE("WIFI", "STA disconnected, reason: %d", disc->reason);
            xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
            xEventGroupSetBits(wifi_event_group, WIFI_DISCONNECTED_BIT);
            if (user_disconnect)
            {
                ESP_LOGI("WIFI", "User disconnect");
                user_disconnect = false;
            }
            else if (disc->reason == 15)
            {
                ESP_LOGE("WIFI", "Wrong password, clearing NVS credentials");
                wrong_password = true;
                nvs_handle_t nvs_handle;
                if (nvs_open("wifi_creds", NVS_READWRITE, &nvs_handle) == ESP_OK)
                {
                    nvs_erase_all(nvs_handle);
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
                }
                esp_wifi_set_mode(WIFI_MODE_APSTA);
                esp_wifi_start();
                vTaskDelay(pdMS_TO_TICKS(1000));
            }
            else // lost connect wifi condition
            {
                ESP_LOGW("WIFI", "WiFi lost, starting reconnect task");
                xTaskCreate(wifi_reconnect_task, "wifi_reconnect_task", 4096, NULL, 5, NULL);
            }
            break;
        }
        }
    }
    else if (event_base == IP_EVENT)
    {
        if (event_id == IP_EVENT_STA_GOT_IP)
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI("WIFI", "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
            xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
            xTaskCreate(stop_server_task, "stop_server_task", 4096, NULL, 5, NULL); // stop web_server delay(10000)
            show_wifi_screen = false;                                               // OLED flag
        }
    }
}
void wifi_connect(const char *ssid, const char *password)
{
    wifi_config_t wifi_sta_config = {0};

    strncpy((char *)wifi_sta_config.sta.ssid, ssid, sizeof(wifi_sta_config.sta.ssid));
    strncpy((char *)wifi_sta_config.sta.password, password, sizeof(wifi_sta_config.sta.password));

    ESP_LOGI(TAG_AP, "Connecting to SSID: %s", ssid);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_sta_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_connect());
}

static const char *auth_mode_type(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode)
    {
    case WIFI_AUTH_OPEN:
        return "OPEN";
    case WIFI_AUTH_WEP:
        return "WEP";
    case WIFI_AUTH_WPA_PSK:
        return "WPA PSK";
    case WIFI_AUTH_WPA2_PSK:
        return "WPA2 PSK";
    case WIFI_AUTH_WPA_WPA2_PSK:
        return "WPA WPA2 PSK";
    case WIFI_AUTH_WPA3_PSK:
        return "WPA3 PSK";
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return "WPA2 WPA3 PSK";
    case WIFI_AUTH_WAPI_PSK:
        return "WAPI PSK";
    default:
        return "UNKNOWN";
    }
}

void wifi_init_apsta(void)
{
    wifi_event_group = xEventGroupCreate();
    if (!wifi_event_group)
    {
        ESP_LOGE(TAG_RE, "Failed to create wifi_event_group");
        return;
    }
    ESP_ERROR_CHECK(nvs_flash_init());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    wifi_config_t wifi_ap_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = WIFI_CHANNEL,
            .password = WIFI_PASS,
            .max_connection = MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = false,
            },
        },
    };
    if (strlen(WIFI_PASS) == 0)
    {
        wifi_ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_ap_config));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG_AP, "SoftAP started. SSID:%s, password:%s", WIFI_SSID, WIFI_PASS);
}

void wifi_scan_task(void *pvParameters)
{
    while (1)
    {

        wifi_ap_record_t connected_info;
        esp_err_t connected = esp_wifi_sta_get_ap_info(&connected_info);
        if (connected == ESP_OK)
        {
            ESP_LOGI(TAG_SCAN, "Connected to SSID: %s. Stop scanning.", (char *)connected_info.ssid);
            vTaskDelete(NULL);
            return;
        }
        wifi_mode_t mode;
        esp_err_t err_mode = esp_wifi_get_mode(&mode);
        if (err_mode != ESP_OK || mode == WIFI_MODE_NULL)
        {
            ESP_LOGE(TAG_SCAN, "Wi-Fi not initialized. Abort scan.");
            vTaskDelete(NULL);
            return;
        }

        wifi_scan_config_t scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = true};
        esp_wifi_scan_start(&scan_config, true);
        ESP_LOGI(TAG_SCAN, "Starting scan...");
        esp_err_t err_scan = esp_wifi_scan_start(&scan_config, true);
        if (err_scan != ESP_OK)
        {
            ESP_LOGE(TAG_SCAN, "Scan start failed: %s", esp_err_to_name(err_scan));
            vTaskDelete(NULL);
            return;
        }

        uint16_t ap_num = MAXIMUM_AP;
        wifi_ap_record_t ap_records[MAXIMUM_AP];
        memset(ap_records, 0, sizeof(ap_records));

        esp_err_t err_get = esp_wifi_scan_get_ap_records(&ap_num, ap_records);
        if (err_get != ESP_OK)
        {
            ESP_LOGE(TAG_SCAN, "Failed to get AP records: %s", esp_err_to_name(err_get));
            vTaskDelete(NULL);
            return;
        }
        ESP_LOGI(TAG_SCAN, "Found %d APs", ap_num);
        vTaskDelay(pdMS_TO_TICKS(5000));
        for (int i = 0; i < ap_num; i++)
        {
            char ssid[33] = {0};
            memcpy(ssid, ap_records[i].ssid, sizeof(ap_records[i].ssid));
            ssid[32] = '\0';
            if (strlen(ssid) == 0)
            {
                ESP_LOGI(TAG_SCAN, "AP %d: SSID: <hidden>, Channel: %d, RSSI: %d, Authmode: %s",
                         i, ap_records[i].primary, ap_records[i].rssi,
                         auth_mode_type(ap_records[i].authmode));
            }
            else
            {
                ESP_LOGI(TAG_SCAN, "AP %d: SSID: %s, Channel: %d, RSSI: %d, Authmode: %s",
                         i, ssid, ap_records[i].primary, ap_records[i].rssi,
                         auth_mode_type(ap_records[i].authmode));
            }
        }
        scanned_ap_count = ap_num;
        memcpy(scanned_aps, ap_records, sizeof(wifi_ap_record_t) * ap_num);
        if (main_task_handle != NULL)
        {
            xTaskNotifyGive(main_task_handle);
        }
    }
    // start_webserver();
    vTaskDelete(NULL);
}
void connect_wifi_nvs()
{
    char ssid[33] = {0};
    char password[65] = {0};
    size_t ssid_len = sizeof(ssid);
    size_t pass_len = sizeof(password);

    nvs_handle_t nvs_handle;
    wifi_ap_record_t ap_info;
    esp_err_t ret = esp_wifi_sta_get_ap_info(&ap_info);
    esp_err_t err = nvs_open("wifi_creds", NVS_READONLY, &nvs_handle);
    if (err == ESP_OK)
    {
        if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len) == ESP_OK &&
            nvs_get_str(nvs_handle, "password", password, &pass_len) == ESP_OK)
        {

            wifi_connect(ssid, password);
            if (ret == ESP_OK)
            {
                ESP_LOGI(TAG_AP, "Connecting to saved wifi: SSID=%s", ssid);
            }
            else
            {
                ESP_LOGI(TAG_RE, "Can't Connecting: %s", ssid);
            }
        }
        else
        {
            ESP_LOGI(TAG_AP, "No wifi credentials in NVS");
            // xTaskCreate(wifi_scan_task, "wifi_scan_task", 8192, NULL, 5, NULL);
            wifi_mode_t mode;
            esp_err_t err_mode = esp_wifi_get_mode(&mode);
            ESP_LOGI(TAG_SCAN, "Current Wi-Fi mode: %d", mode);
            ESP_LOGI(TAG_AP, "Cannot open NVS");
        }
        nvs_close(nvs_handle);
    }
    else
    {
        // xTaskCreate(wifi_scan_task, "wifi_scan_task", 8192, NULL, 5, NULL);
        wifi_mode_t mode;
        esp_err_t err_mode = esp_wifi_get_mode(&mode);
        ESP_LOGI(TAG_SCAN, "Current Wi-Fi mode: %d", mode);
        ESP_LOGI(TAG_AP, "Cannot open NVS");
    }
}
void wifi_reconnect_task()
{
    char ssid[33] = {0}, password[65] = {0};
    size_t ssid_len = sizeof(ssid), pass_len = sizeof(password);
    nvs_handle_t nvs_handle;

    while (1)
    {
        if (nvs_open("wifi_creds", NVS_READONLY, &nvs_handle) == ESP_OK)
        {
            if (nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len) == ESP_OK &&
                nvs_get_str(nvs_handle, "password", password, &pass_len) == ESP_OK)
            {
                wifi_config_t wifi_config = {};
                strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
                strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password));
                esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
                esp_wifi_connect();
                ESP_LOGI("WIFI", "Reconnecting to saved SSID: %s", ssid);
            }
            nvs_close(nvs_handle);
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
    vTaskDelete(NULL);
}
