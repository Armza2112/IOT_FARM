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
#include <esp_wifi.h>
#include <time.h>
#include "../time_manage/time_manage.h"
#include "../data_esp32/data_esp32.h"

#define TAG "DEVICE_API"

static char time_set[2];
static char *mqttarr[2];

char uuid[64], key[32], server[64], user[32], pass[32];

double sal;
int ry1[4], ry2[4], ry3[4], ry4[4], pca[8];
char device_time[32], device_exp[16];
static char last_device_date[16] = "";
bool en;
bool check_time_device = false;
bool check_reg_device = false;
static bool already_post = false;
bool server_connected = false;
bool check_server_alive()

{
    ESP_LOGI("CHECK", "Checking server alive...");

    esp_http_client_config_t config = {
        .url = "http://192.168.1.132:4000/",
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = 3000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL)
    {
        ESP_LOGE("CHECK", "Failed to init HTTP client");
        return false;
    }

    ESP_LOGI("CHECK", "Performing HTTP HEAD request...");
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI("CHECK", "Server reachable, HTTP status: %d", status);
        esp_http_client_cleanup(client);
        server_connected = true;
        return true;
    }
    else
    {
        ESP_LOGW("CHECK", "Server not reachable: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        server_connected = false;
        return false;
    }
}

void post_uuid()
{
    load_uuid_from_spiffs();
    char post_data[128];
    snprintf(post_data, sizeof(post_data), "{\"uuid\": \"%s\"}", uuidcid);

    esp_http_client_config_t config = {
        .url = "http://192.168.1.132:4000/device/uuid",
        .method = HTTP_METHOD_POST,
        // .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");

    ESP_LOGI(TAG, "POST body: %s (len=%d)", post_data, strlen(post_data));

    esp_err_t err = esp_http_client_open(client, strlen(post_data));
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    int wlen = esp_http_client_write(client, post_data, strlen(post_data));
    if (wlen < 0)
    {
        ESP_LOGE(TAG, "Write failed");
        esp_http_client_cleanup(client);
        return;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP POST Status = %d", status);

    char buffer[2048];
    int content_len = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
    if (content_len >= 0)
    {
        buffer[content_len] = 0;
        ESP_LOGI(TAG, "Response:\n%s", buffer);
    }

    esp_http_client_cleanup(client);
}
void post_time_set()
{
    wifi_ap_record_t ap_info;
    bool connect = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (connect)
    {
        if (!time_correct)
        {
            char post_data[50];
            snprintf(post_data, sizeof(post_data), "{\"set_t\": \"%s\"}", time_correct ? "true" : "false");

            esp_http_client_config_t config = {
                .url = "http://192.168.1.132:4000/device/time_set",
                .method = HTTP_METHOD_POST,
                // .crt_bundle_attach = esp_crt_bundle_attach,
                .timeout_ms = 10000,
            };

            esp_http_client_handle_t client = esp_http_client_init(&config);
            esp_http_client_set_header(client, "Content-Type", "application/json");

            ESP_LOGI(TAG, "POST body: %s (len=%d)", post_data, strlen(post_data));

            esp_err_t err = esp_http_client_open(client, strlen(post_data));
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
                esp_http_client_cleanup(client);
                return;
            }

            int wlen = esp_http_client_write(client, post_data, strlen(post_data));
            if (wlen < 0)
            {
                ESP_LOGE(TAG, "Write failed");
                esp_http_client_cleanup(client);
                return;
            }

            esp_http_client_fetch_headers(client);
            int status = esp_http_client_get_status_code(client);
            ESP_LOGI(TAG, "HTTP POST Status = %d", status);

            char buffer[2048];
            int content_len = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
            if (content_len >= 0)
            {
                buffer[content_len] = 0;
                ESP_LOGI(TAG, "Response:\n%s", buffer);
            }

            esp_http_client_cleanup(client);
        }
    }
    else
    {
        ESP_LOGW(TAG, "Cant connect wifi(not post server)");
    }
}
void get_device_reg()
{
    esp_http_client_config_t config = {
        .url = "http://192.168.1.132:4000/device/get/reg",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    esp_http_client_fetch_headers(client);

    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "HTTP Status = %d, content_length = %d", status, content_length);

    int buf_len = (content_length > 0 && content_length < 4096) ? content_length + 1 : 4096;
    char *buffer = malloc(buf_len);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Failed to allocate buffer");
        esp_http_client_cleanup(client);
        return;
    }

    int len = esp_http_client_read_response(client, buffer, buf_len - 1);
    if (len >= 0)
    {
        buffer[len] = '\0';
        ESP_LOGI(TAG, "Response body: %s", buffer);

        cJSON *root = cJSON_Parse(buffer);
        if (root)
        {
            cJSON *id = cJSON_GetObjectItem(root, "id");
            cJSON *m = cJSON_GetObjectItem(root, "m");

            if (id && m)
            {
                cJSON *u = cJSON_GetObjectItem(id, "u");
                cJSON *k = cJSON_GetObjectItem(id, "k");
                cJSON *s = cJSON_GetObjectItem(m, "s");
                cJSON *un = cJSON_GetObjectItem(m, "un");
                cJSON *pw = cJSON_GetObjectItem(m, "pw");

                if (u && k && s && un && pw)
                {
                    strncpy(uuid, u->valuestring, sizeof(uuid) - 1);
                    uuid[sizeof(uuid) - 1] = '\0';

                    strncpy(key, k->valuestring, sizeof(key) - 1);
                    key[sizeof(key) - 1] = '\0';

                    strncpy(server, s->valuestring, sizeof(server) - 1);
                    server[sizeof(server) - 1] = '\0';

                    strncpy(user, un->valuestring, sizeof(user) - 1);
                    user[sizeof(user) - 1] = '\0';

                    strncpy(pass, pw->valuestring, sizeof(pass) - 1);
                    pass[sizeof(pass) - 1] = '\0';

                    ESP_LOGI(TAG, "UUID: %s", uuid);
                    ESP_LOGI(TAG, "Key: %s", key);
                    ESP_LOGI(TAG, "MQTT Server: %s", server);
                    ESP_LOGI(TAG, "MQTT User: %s", user);
                    ESP_LOGI(TAG, "MQTT Pass: %s", pass);
                }
            }
            cJSON_Delete(root);
        }
    }

    free(buffer);
    esp_http_client_cleanup(client);
    check_reg_device=true;
}

void get_device_data()
{
    esp_http_client_config_t config = {
        .url = "http://192.168.1.132:4000/device/get/data",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return;
    }

    esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);
    ESP_LOGI(TAG, "HTTP Status = %d, content_length = %d", status, content_length);

    int buf_len = (content_length > 0 && content_length < 4096) ? content_length + 1 : 4096;
    char *buffer = malloc(buf_len);
    if (!buffer)
    {
        ESP_LOGE(TAG, "Malloc failed!");
        esp_http_client_cleanup(client);
        return;
    }

    int read_len = esp_http_client_read_response(client, buffer, buf_len - 1);
    if (read_len >= 0)
    {
        buffer[read_len] = '\0';
        ESP_LOGI(TAG, "Response body: %s", buffer);

        cJSON *root = cJSON_Parse(buffer);
        if (root)
        {
            cJSON *s = cJSON_GetObjectItem(root, "s");
            if (s)
                sal = cJSON_GetObjectItem(s, "sa")->valuedouble;

            cJSON *r = cJSON_GetObjectItem(root, "r");
            if (r)
            {
                for (int i = 0; i < 4; i++)
                {
                    ry1[i] = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "ry1"), i)->valueint;
                    ry2[i] = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "ry2"), i)->valueint;
                    ry3[i] = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "ry3"), i)->valueint;
                    ry4[i] = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "ry4"), i)->valueint;
                }
                for (int i = 0; i < 8; i++)
                {
                    cJSON *item = cJSON_GetArrayItem(cJSON_GetObjectItem(r, "p"), i);
                    if (item)
                        pca[i] = item->valueint;
                }
            }

            strncpy(device_time, cJSON_GetObjectItem(root, "t")->valuestring, sizeof(device_time) - 1);
            strncpy(device_exp, cJSON_GetObjectItem(root, "exp")->valuestring, sizeof(device_exp) - 1);
            en = cJSON_IsTrue(cJSON_GetObjectItem(root, "en"));

            ESP_LOGI(TAG, "Salinity: %.2f", sal);
            ESP_LOGI(TAG, "Ry1: [%d,%d,%d,%d]", ry1[0], ry1[1], ry1[2], ry1[3]);
            ESP_LOGI(TAG, "Ry2: [%d,%d,%d,%d]", ry2[0], ry2[1], ry2[2], ry2[3]);
            ESP_LOGI(TAG, "Ry3: [%d,%d,%d,%d]", ry3[0], ry3[1], ry3[2], ry3[3]);
            ESP_LOGI(TAG, "Ry4: [%d,%d,%d,%d]", ry4[0], ry4[1], ry4[2], ry4[3]);
            ESP_LOGI(TAG, "PCA: [%d,%d,%d,%d,%d,%d,%d,%d]",
                     pca[0], pca[1], pca[2], pca[3], pca[4], pca[5], pca[6], pca[7]);
            ESP_LOGI(TAG, "Time: %s", device_time);
            ESP_LOGI(TAG, "Exp: %s", device_exp);
            ESP_LOGI(TAG, "Enable: %s", en ? "true" : "false");

            cJSON_Delete(root);
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to read response");
    }

    free(buffer);
    esp_http_client_cleanup(client);
    check_time_device = true;
}

void http_task()
{
    wifi_ap_record_t ap_info;
    vTaskDelay(pdMS_TO_TICKS(5000));
    while (1)
    {
        bool connect = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
        // if connect run task else if disconnect dont send anythings
        if (connect)
        {
            // if sever alive run task else if server dead dont send anythings
            if (check_server_alive())
            {
                if (!already_post)
                {
                    post_uuid();
                    get_device_reg();
                    get_device_data();
                    strncpy(last_device_date, device_time + 8, 2); // 2025-09-04T10:30:00Z --> 04
                    last_device_date[2] = '\0';
                    already_post = true;
                }
                else
                {
                    while (!already_check_time) // waiting ds1307 set time
                    {
                        ESP_LOGW(TAG, "Waitind time device");
                        vTaskDelay(pdMS_TO_TICKS(5000));
                    }
                    /*send data from server everyday*/
                    if (strcmp(date_ds1307, last_device_date) != 0) // (current date) check with (last device date) if same return 1;
                    {
                        ESP_LOGI(TAG, "New day: %s → %s", last_device_date, date_ds1307);
                        get_device_data();
                        strncpy(last_device_date, date_ds1307, sizeof(last_device_date) - 1);
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Already fetched data today (%s)", date_ds1307);
                    }
                    ESP_LOGI(TAG, "Already Post");
                }
            }
            else
            {
                ESP_LOGW(TAG, "Cant post http(disconnect server)");
                already_post = false;
            }
        }
        else
        {
            ESP_LOGW(TAG, "Cant post http(disconnect wifi)");
            already_post = false;
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}