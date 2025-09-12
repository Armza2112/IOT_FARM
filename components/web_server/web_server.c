#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "../wifi_manager/wifi_manager.h"
#include "../data_esp32/data_esp32.h"
#include "../device_api/device_api.h"

esp_err_t restart_handle(httpd_req_t *req);
esp_err_t connect_post_handler(httpd_req_t *req);
httpd_handle_t server_handle = NULL; // global

static const char *TAG_INFO = "INFO";
static const char *TAG_AP = "wifi_softap_web";

static bool web_start = false;

void replace_placeholder(char *line, size_t line_size,
                         const char *key, const char *value)
{
    char temp[512];
    char *pos;

    while ((pos = strstr(line, key)) != NULL)
    {
        snprintf(temp, sizeof(temp), "%.*s%s%s",
                 (int)(pos - line), line, value, pos + strlen(key));
        strncpy(line, temp, line_size - 1);
        line[line_size - 1] = '\0';
    }
}

esp_err_t home(httpd_req_t *req)
{
    char ssid[33] = "Not connected";
    nvs_handle_t nvs_handle;
    size_t ssid_len = sizeof(ssid);
    char connect_str[6];
    if (nvs_open("wifi_creds", NVS_READONLY, &nvs_handle) == ESP_OK)
    {
        nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
        nvs_close(nvs_handle);
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    int rssi = 0;
    wifi_ap_record_t ap_info;
    bool connect = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (connect)
        strcpy(connect_str, "true");
    else
        strcpy(connect_str, "false");
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        rssi = ap_info.rssi;

    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);

    FILE *f = fopen("/spiffs/home.html", "rb");
    if (!f)
        return httpd_resp_send(req, "Cannot open home.html", HTTPD_RESP_USE_STRLEN);
    httpd_resp_set_type(req, "text/html");

    char line[512];
    while (fgets(line, sizeof(line), f))
    {
        replace_placeholder(line, sizeof(line), "%SSID%", ssid);
        replace_placeholder(line, sizeof(line), "%MAC%", mac_str);
        replace_placeholder(line, sizeof(line), "%DEVICE_ID%", uuidcid); // get uuid form spiffs (device_api.c)
        replace_placeholder(line, sizeof(line), "%RSSI%", rssi_str);
        replace_placeholder(line, sizeof(line), "%KEY%", key);
        replace_placeholder(line, sizeof(line), "%WIFI%", connect_str);
        if (httpd_resp_send_chunk(req, line, strlen(line)) != ESP_OK)
        {
            fclose(f);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}
esp_err_t wifi_manage(httpd_req_t *req)
{
    char ssid[33] = "Not connected";
    nvs_handle_t nvs_handle;
    size_t ssid_len = sizeof(ssid);
    char ssid_display[33] = "Not connected";
    char buf[2048];
    char connect_str[6];
    if (nvs_open("wifi_creds", NVS_READONLY, &nvs_handle) == ESP_OK)
    {
        nvs_get_str(nvs_handle, "ssid", ssid, &ssid_len);
        nvs_close(nvs_handle);
    }
    int rssi = 0;
    wifi_ap_record_t ap_info;
    bool connect = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (connect)
        strcpy(connect_str, "true");
    else
        strcpy(connect_str, "false");

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
        rssi = ap_info.rssi;

    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
    FILE *f = fopen("/spiffs/wifi_manage.html", "r");
    if (!f)
        return httpd_resp_send_500(req);
    char line[1024];
    httpd_resp_set_type(req, "text/html");

    while (fgets(line, sizeof(line), f))
    {
        replace_placeholder(line, sizeof(line), "%SSID%", ssid);
        replace_placeholder(line, sizeof(line), "%RSSI%", rssi_str);
        replace_placeholder(line, sizeof(line), "%WIFI%", connect_str);

        if (strstr(line, "%OPTIONS%"))
        {
            char options[2048] = {0};
            for (int i = 0; i < scanned_ap_count; i++)
            {
                char opt[128];
                snprintf(opt, sizeof(opt), "<option value=\"%s\">%s (%d dBm)</option>",
                         (char *)scanned_aps[i].ssid, (char *)scanned_aps[i].ssid, scanned_aps[i].rssi);
                strlcat(options, opt, sizeof(options));
            }

            replace_placeholder(line, sizeof(line), "%OPTIONS%", options);
        }

        if (httpd_resp_send_chunk(req, line, strlen(line)) != ESP_OK)
        {
            fclose(f);
            return ESP_FAIL;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    httpd_resp_send_chunk(req, NULL, 0);
    fclose(f);
    return ESP_OK;
}
esp_err_t connect_post_handler(httpd_req_t *req)
{
    char buf[256];
    int ret, remaining = req->content_len;
    char ssid[33] = {0};
    char password[65] = {0};

    int idx = 0;
    while (remaining > 0 && idx < sizeof(buf) - 1)
    {
        ret = httpd_req_recv(req, buf + idx, remaining > sizeof(buf) - idx - 1 ? sizeof(buf) - idx - 1 : remaining);
        if (ret <= 0)
        {
            return ESP_FAIL;
        }
        remaining -= ret;
        idx += ret;
    }
    buf[idx] = '\0';

    ESP_LOGI(TAG_AP, "Received POST data: %s", buf);

    char *ssid_start = strstr(buf, "ssid=");
    char *pass_start = strstr(buf, "password=");
    if (!ssid_start)
        return ESP_FAIL;

    ssid_start += strlen("ssid=");
    char *ssid_end = strchr(ssid_start, '&');
    if (ssid_end)
    {
        int len = ssid_end - ssid_start;
        if (len > 32)
            len = 32;
        strncpy(ssid, ssid_start, len);
        ssid[len] = '\0';
    }
    else
    {
        strncpy(ssid, ssid_start, 32);
        ssid[32] = '\0';
    }

    if (pass_start)
    {
        pass_start += strlen("password=");
        char *pass_end = strchr(pass_start, '&');
        int len = pass_end ? (pass_end - pass_start) : strlen(pass_start);
        if (len > 64)
            len = 64;
        strncpy(password, pass_start, len);
        password[len] = '\0';
    }

    ESP_LOGI(TAG_AP, "Parsed SSID: %s", ssid);

    wifi_connect(ssid, password);
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (wrong_password == true)
    {
        ESP_LOGE(TAG_AP, "Wrong password detected, sending JSON to client");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"fail\",\"reason\":\"wrong_password\"}");
        wrong_password = false;
    }
    else if (bits & WIFI_CONNECTED_BIT)
    {
        nvs_handle_t nvs_handle;
        esp_err_t err = nvs_open("wifi_creds", NVS_READWRITE, &nvs_handle);
        if (err == ESP_OK)
        {
            nvs_set_str(nvs_handle, "ssid", ssid);
            nvs_set_str(nvs_handle, "password", password);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
        }
        else
        {
            ESP_LOGI(TAG_AP, "Fail to save in NVS%s", esp_err_to_name(err));
        }
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    }
    else
    {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"fail\",\"reason\":\"timeout\"}");
    }

    return ESP_OK;
}
esp_err_t restart_handle(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"restart\"}");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}
void disconnect_wifi_task(void *pvParameter)
{

    ESP_LOGI(TAG_AP, "Disconnect wifi task running");
    user_disconnect = true;
    esp_wifi_disconnect();
    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_DISCONNECTED_BIT,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(5000));
    if (bits & WIFI_DISCONNECTED_BIT)
    {
        ESP_LOGI(TAG_AP, "STA disconnected, clearing NVS");
        nvs_handle_t nvs_handle;
        if (nvs_open("wifi_creds", NVS_READWRITE, &nvs_handle) == ESP_OK)
        {
            nvs_erase_all(nvs_handle);
            nvs_commit(nvs_handle);
            nvs_close(nvs_handle);
            ESP_LOGI("NVS", "All Wi-Fi credentials erased.");
        }

        esp_wifi_set_mode(WIFI_MODE_APSTA);
        esp_wifi_start();
        xTaskCreate(wifi_scan_task, "wifi_scan_task", 8192, NULL, 5, NULL);
    }
    vTaskDelete(NULL);
}

esp_err_t disconnect_handle(httpd_req_t *req)
{
    ESP_LOGI(TAG_AP, "Disconnect");
    xTaskCreate(disconnect_wifi_task, "disconnect_wifi_task", 8192, NULL, 5, NULL);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"disconnecting\"}");

    return ESP_OK;
}
esp_err_t log_file_handler(httpd_req_t *req)
{
    FILE *f = fopen("/spiffs/log.txt", "r");
    if (!f)
    {
        return httpd_resp_send(req, "Cannot open log.txt", HTTPD_RESP_USE_STRLEN);
    }

    char line[128];
    httpd_resp_sendstr_chunk(req, "<html><body><pre>");

    while (fgets(line, sizeof(line), f))
    {
        httpd_resp_sendstr_chunk(req, line);
    }

    httpd_resp_sendstr_chunk(req, "</pre></body></html>");
    httpd_resp_sendstr_chunk(req, NULL);
    fclose(f);
    return ESP_OK;
}
esp_err_t spiffs_get_handler(httpd_req_t *req)
{
    char filepath[2048];
    snprintf(filepath, sizeof(filepath), "/spiffs%s", req->uri);
    ESP_LOGI("SPIFFS", "Requested file: %s", filepath);

    FILE *file = fopen(filepath, "rb");
    if (!file)
    {
        ESP_LOGW("SPIFFS", "File not found: %s", filepath);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }
    if (strstr(filepath, ".png"))
        httpd_resp_set_type(req, "image/png");
    else if (strstr(filepath, ".ttf"))
        httpd_resp_set_type(req, "font/ttf");
    else if (strstr(filepath, ".css"))
        httpd_resp_set_type(req, "text/css");
    else if (strstr(filepath, ".js"))
        httpd_resp_set_type(req, "application/javascript");
    else if (strstr(filepath, ".html"))
        httpd_resp_set_type(req, "text/html");

    char buffer[2048];
    size_t read_bytes;
    while ((read_bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        if (httpd_resp_send_chunk(req, buffer, read_bytes) != ESP_OK)
        {
            fclose(file);
            return ESP_FAIL;
        }
    }
    ESP_LOGI("SPIFFS", "Requested file: %s", filepath);

    httpd_resp_send_chunk(req, NULL, 0);
    fclose(file);
    return ESP_OK;
}

httpd_handle_t start_webserver(void)
{
    web_start = true;
    if (server_handle != NULL)
    {
        ESP_LOGW("WEBSERVER", "Server already running");
        return server_handle;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    config.stack_size = 16384;
    config.max_uri_handlers = 32;
    if (httpd_start(&server, &config) == ESP_OK)
    {
        httpd_uri_t home_uri = {
            .uri = "/",
            .method = HTTP_GET,
            .handler = home,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &home_uri);
        httpd_uri_t uri_get = {
            .uri = "/wifimanage",
            .method = HTTP_GET,
            .handler = wifi_manage,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_get);
        httpd_uri_t uri_post = {
            .uri = "/connect",
            .method = HTTP_POST,
            .handler = connect_post_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &uri_post);
        httpd_uri_t disconnect_get = {
            .uri = "/disconnect",
            .method = HTTP_POST,
            .handler = disconnect_handle,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &disconnect_get);
        httpd_uri_t restart_get = {
            .uri = "/restart",
            .method = HTTP_GET,
            .handler = restart_handle,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &restart_get);
        httpd_uri_t log_uri = {
            .uri = "/log",
            .method = HTTP_GET,
            .handler = log_file_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &log_uri);
        httpd_uri_t logo_uri = {
            .uri = "/assets/img/LOGO.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &logo_uri);
        httpd_uri_t font_uri = {
            .uri = "/assets/fnt/header.ttf",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &font_uri);
        httpd_uri_t detail_uri = {
            .uri = "/assets/fnt/detail.ttf",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &detail_uri);
        httpd_uri_t online_uri = {
            .uri = "/assets/img/online.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &online_uri);
        httpd_uri_t offline_uri = {
            .uri = "/assets/img/offline.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &offline_uri);
        httpd_uri_t wrong_uri = {
            .uri = "/assets/img/wrong.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &wrong_uri);
        httpd_uri_t correct_uri = {
            .uri = "/assets/img/correct.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &correct_uri);
        httpd_uri_t load_uri = {
            .uri = "/assets/img/loading.gif",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &load_uri);
        httpd_uri_t arrow_uri = {
            .uri = "/assets/img/arrow.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &arrow_uri);
        httpd_uri_t wifi_1_uri = {
            .uri = "/assets/img/wifi_1.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &wifi_1_uri);
        httpd_uri_t wifi_2_uri = {
            .uri = "/assets/img/wifi_2.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &wifi_2_uri);
        httpd_uri_t wifi_3_uri = {
            .uri = "/assets/img/wifi_3.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &wifi_3_uri);
        httpd_uri_t wifi_4_uri = {
            .uri = "/assets/img/wifi_4.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &wifi_4_uri);
        httpd_uri_t alert_uri = {
            .uri = "/assets/img/alert.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &alert_uri);
        httpd_uri_t wifi_fail_uri = {
            .uri = "/assets/img/wifi_fail.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &wifi_fail_uri);
        httpd_uri_t hide_uri = {
            .uri = "/assets/img/hide.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &hide_uri);
        httpd_uri_t unhide_uri = {
            .uri = "/assets/img/unhide.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &unhide_uri);
        httpd_uri_t down_uri = {
            .uri = "/assets/img/down.png",
            .method = HTTP_GET,
            .handler = spiffs_get_handler,
            .user_ctx = NULL};
        httpd_register_uri_handler(server, &down_uri);
    }
    return server;
}
void stop_webserver(void)
{
    if (server_handle != NULL)
    {
        httpd_stop(server_handle);
        server_handle = NULL;
        ESP_LOGI("WEBSERVER", "Server stopped");
    }
    web_start = false;
}
void stop_server_task(void *pvParameter)
{
    if (!web_start)
    {
        stop_webserver();
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        wifi_mode_t mode;
        esp_err_t err_mode = esp_wifi_get_mode(&mode);
        ESP_LOGI("TAG_SCAN", "Current Wi-Fi mode: %d", mode);
    }
    else
    {
        ESP_LOGI("WEB", "Set wifi on web");
    }
    vTaskDelete(NULL);
}