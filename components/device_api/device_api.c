#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_spiffs.h"
#include "esp_crt_bundle.h"
#define TAG "DEVICE_API"
#define UUID_STR_LEN 37
static const char *TAGUUID = "UUID_MANAGER";

static char uuidcid[UUID_STR_LEN] = {0};
static char *mqttarr[2];

// char *load_cert_from_spiffs(void)
// {
//     FILE *f = fopen("/spiffs/server.crt", "r");
//     if (!f)
//     {
//         ESP_LOGE(TAG, "Cannot open certificate file");
//         return NULL;
//     }

//     fseek(f, 0, SEEK_END);
//     size_t len = ftell(f);
//     fseek(f, 0, SEEK_SET);

//     char *buf = malloc(len + 1);
//     if (!buf)
//     {
//         fclose(f);
//         ESP_LOGE(TAG, "Cannot allocate memory for certificate");
//         return NULL;
//     }

//     fread(buf, 1, len, f);
//     buf[len] = '\0';
//     fclose(f);
//     return buf;
// }

void load_uuid_from_spiffs(void)
{
    FILE *f = fopen("/spiffs/uuid.txt", "r");
    if (!f)
    {
        ESP_LOGE(TAGUUID, "UUID file not found");
        return;
    }

    fgets(uuidcid, sizeof(uuidcid), f);
    fclose(f);
    uuidcid[strcspn(uuidcid, "\n")] = 0;
    ESP_LOGI(TAGUUID, "Loaded UUID: %s", uuidcid);
}
void send_uuid_to_postman()
{
    char post_data[128];
    snprintf(post_data, sizeof(post_data), "{\"uuid\": \"%s\"}", uuidcid);

    esp_http_client_config_t config = {
        .url = "http://192.168.1.119:4000/api/v1/device",
        .method = HTTP_METHOD_POST,
        // .crt_bundle_attach = esp_crt_bundle_attach, 
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    ESP_LOGI(TAG, "POST body: %s (len=%d)", post_data, strlen(post_data));

    // 🔹 เปิด connection พร้อมระบุขนาด body
    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    // 🔹 เขียน body เข้าไปเอง
    int wlen = esp_http_client_write(client, post_data, strlen(post_data));
    if (wlen < 0) {
        ESP_LOGE(TAG, "Write failed");
        esp_http_client_cleanup(client);
        return;
    }

    // 🔹 ดึง response
    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP POST Status = %d", status);

    char buffer[2048];
    int content_len = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
    if (content_len >= 0) {
        buffer[content_len] = 0;
        ESP_LOGI(TAG, "Response:\n%s", buffer);
    }

    esp_http_client_cleanup(client);
}

// static void load_mqtt_credentials(void)
// {
//     FILE *f = fopen("/spiffs/settingsmqtt.txt", "r");
//     if (!f)
//     {
//         ESP_LOGW(TAG, "No MQTT settings file found.");
//         return;
//     }

//     char line[128];
//     if (fgets(line, sizeof(line), f))
//     {
//         char *token = strtok(line, ",");
//         int i = 0;
//         while (token && i < 2)
//         {
//             mqttarr[i] = strdup(token);
//             token = strtok(NULL, ",");
//             i++;
//         }
//     }
//     fclose(f);
//     ESP_LOGI(TAG, "MQTT credentials loaded: user=%s, pass=%s", mqttarr[0], mqttarr[1]);
// }

// static void save_mqtt_credentials(const char *user, const char *pass)
// {
//     FILE *f = fopen("/spiffs/settingsmqtt.txt", "w");
//     if (!f)
//         return;

//     fprintf(f, "%s,%s", user, pass);
//     fclose(f);
//     ESP_LOGI(TAG, "Saved MQTT credentials: %s / %s", user, pass);
// }

// static char *build_device_json(const char *macStr,
//                                const char *ssid,
//                                const char *ip,
//                                int rssi,
//                                int versionpro,
//                                bool boardstatus[4])
// {
//     cJSON *root = cJSON_CreateObject();
//     if (!root)
//         return NULL;

//     cJSON_AddStringToObject(root, "mac", macStr);
//     cJSON_AddStringToObject(root, "cid", uuidcid);
//     cJSON_AddStringToObject(root, "wifi", ssid);
//     cJSON_AddStringToObject(root, "wifiip", ip);
//     cJSON_AddNumberToObject(root, "wifiss", rssi);
//     cJSON_AddNumberToObject(root, "v", versionpro);

//     cJSON *board = cJSON_CreateArray();
//     for (int i = 0; i < 4; i++)
//         cJSON_AddItemToArray(board, cJSON_CreateBool(boardstatus[i]));
//     cJSON_AddItemToObject(root, "borad", board);

//     cJSON_AddNumberToObject(root, "efhs", 0);
//     cJSON_AddNumberToObject(root, "sfhs", 0);

//     char *json_str = cJSON_PrintUnformatted(root);
//     cJSON_Delete(root);
//     return json_str;
// }

// void device_register_or_update(const char *ssid,
//                                const char *ip,
//                                int rssi,
//                                int versionpro,
//                                bool boardstatus[4])
// {
//     char *cert_pem = load_cert_from_spiffs();
//     if (!cert_pem)
//     {
//         ESP_LOGE(TAG, "Cannot load certificate, aborting HTTP request");
//         return;
//     }
//     ESP_LOGI(TAG, "Loaded cert:\n%s", cert_pem);

//     uint8_t mac[6];
//     esp_read_mac(mac, ESP_MAC_WIFI_STA);
//     char macStr[20];
//     sprintf(macStr, "%02x:%02x:%02x:%02x:%02x:%02x",
//             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

//     char urlcheck[200];
//     snprintf(urlcheck, sizeof(urlcheck),
//              "https://192.168.1.119:3500/api/v1/device?mac=%s&cid=%s",
//              macStr, uuidcid);

//     esp_http_client_config_t cfg_get = {
//         .url = urlcheck,
//         .cert_pem = cert_pem,

//     };
//     esp_log_level_set("esp-tls", ESP_LOG_DEBUG);
//     esp_log_level_set("esp-tls-mbedtls", ESP_LOG_DEBUG);

//     esp_http_client_handle_t client = esp_http_client_init(&cfg_get);
//     esp_http_client_set_method(client, HTTP_METHOD_GET);

//     esp_err_t err = esp_http_client_open(client, 0);
//     int status = -1;
//     if (err == ESP_OK)
//     {
//         esp_http_client_fetch_headers(client);
//         status = esp_http_client_get_status_code(client);
//     }
//     esp_http_client_cleanup(client);

//     char *json_data = build_device_json(macStr, ssid, ip, rssi, versionpro, boardstatus);
//     if (!json_data)
//     {
//         free(cert_pem);
//         return;
//     }

//     const char *method = (status == 200) ? "PUT" : "POST";
//     ESP_LOGI(TAG, "Device %s (%s)", (status == 200) ? "found, updating" : "not found, registering", method);

//     esp_http_client_config_t cfg_post = {
//         .url = "https://192.168.1.119:3500/api/v1/device",
//         .cert_pem = cert_pem,

//     };
//     client = esp_http_client_init(&cfg_post);
//     esp_http_client_set_method(client, (status == 200) ? HTTP_METHOD_PUT : HTTP_METHOD_POST);
//     esp_http_client_set_header(client, "Content-Type", "application/json");
//     esp_http_client_set_post_field(client, json_data, strlen(json_data));

//     err = esp_http_client_perform(client);
//     if (err == ESP_OK)
//     {
//         int resp_status = esp_http_client_get_status_code(client);
//         ESP_LOGI(TAG, "HTTP %s status = %d", method, resp_status);

//         if (resp_status == 200 || resp_status == 201)
//         {
//             int len = esp_http_client_get_content_length(client);
//             char *resp_buf = malloc(len + 1);
//             if (resp_buf)
//             {
//                 esp_http_client_read_response(client, resp_buf, len);
//                 resp_buf[len] = '\0';

//                 cJSON *root = cJSON_Parse(resp_buf);
//                 cJSON *data = cJSON_GetObjectItem(root, "data");
//                 if (data)
//                 {
//                     const char *user = cJSON_GetObjectItem(data, "username")->valuestring;
//                     const char *pass = cJSON_GetObjectItem(data, "clientid")->valuestring;
//                     save_mqtt_credentials(user, pass);
//                 }
//                 cJSON_Delete(root);
//                 free(resp_buf);
//             }
//         }
//     }

//     esp_http_client_cleanup(client);
//     free(json_data);
//     free(cert_pem);

//     load_mqtt_credentials();
// }
