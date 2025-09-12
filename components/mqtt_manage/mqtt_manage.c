#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_spiffs.h"
#include "esp_log.h"
#include "../do7019_manage/do7019_manage.h"
#include "../wifi_manager/wifi_manager.h"
#include "../time_manage/time_manage.h"
#include "../ryr404a_manage/ryr404a_manage.h"
#include "../pca9557_manage/pca9557_manage.h"
#include "../i2c_manage/i2c_manage.h"
#include "../device_api/device_api.h"
#include <math.h>

bool mqtt_connected = false; // flag mqtt connect
bool send_data = false;      // flag send data mqtt to oled
static const char *TAG = "MQTT";
// function
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void mqtt_ryr404a_callback(const char *topic, const char *payload);
void mqtt_pca9557_callback(const char *topic, const char *payload);
void mqtt_task();
void mqtt_spiffs();

// declare var

esp_mqtt_client_handle_t mqtt_client = NULL;

void mqtt_init()
{
    // wifi_event_group = xEventGroupCreate();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = server,
        .credentials.username = user,
        .credentials.authentication.password = pass,
        //     .port = 1883,
        .network.disable_auto_reconnect = false,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id)
    {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        mqtt_connected = true;
        // iotlf/key/relay/controls
        char topic_controls[64];
        snprintf(topic_controls, sizeof(topic_controls), "iotlf/%s/relay/controls/#", key);
        esp_mqtt_client_subscribe(event->client, topic_controls, 2);
        xTaskCreate(mqtt_spiffs, "mqtt_spiffs", 16384, NULL, 5, NULL);
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        ESP_LOGI(TAG, "MQTT Disconnected");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "Message published (msg_id=%d)", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
    {
        char *topic = strndup(event->topic, event->topic_len);
        if (!topic)
            break;

        char *payload = strndup(event->data, event->data_len);
        if (!payload)
        {
            free(topic);
            break;
        }

        ESP_LOGI("MQTT", "TOPIC=%s", topic);
        ESP_LOGI("MQTT", "DATA=%s", payload);
        char topic_ryr[64]; // topic for ryr + key
        snprintf(topic_ryr, sizeof(topic_ryr), "iotlf/%s/relay/controls/ryr404a", key);
        char topic_pca[64]; // topic for pca+key
        snprintf(topic_pca, sizeof(topic_pca), "iotlf/%s/relay/controls/pca9557", key);
        if (strcmp(topic, topic_ryr) == 0) // check between topic recived and topic controls isn't match?
        {
            mqtt_ryr404a_callback(topic, payload);
        }
        else if (strcmp(topic, topic_pca) == 0)
        {
            while (i2c_busy)
            {
                vTaskDelay(pdMS_TO_TICKS(5));
            }
            i2c_busy = true;
            mqtt_pca9557_callback(topic, payload);
            release_task_i2c();
            i2c_busy = false;
        }
        free(payload);
        free(topic);
        break;
    }
    default:
        ESP_LOGI(TAG, "Other MQTT event id:%ld", (long)event_id);
        break;
    }
}

// do7019
void save_to_spiffs(const char *payload) // test!!!
{
    FILE *f = fopen("/spiffs/unsend.txt", "a");
    if (f == NULL)
    {
        ESP_LOGE("SPIFFS", "Failed to open unsent.txt for appending");
        return;
    }
    if (fprintf(f, "%s\n", payload) < 0)
    {
        ESP_LOGE("SPIFFS", "Failed to write payload to unsent.txt");
    }
    else
    {
        ESP_LOGI("SPIFFS", "Saved payload to unsent.txt: %s", payload);
    }

    fclose(f);
}
bool mqtt_safe_publish(const char *payload) // test
{
    wifi_ap_record_t ap_info;
    bool connect = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    bool server_ok = check_server_alive();
    // iotlf/key/sensor/rt
    char topic_sensor[64]; // topic for sensor
    int msg_id = -1;
    snprintf(topic_sensor, sizeof(topic_sensor), "iotlf/%s/sensor/rt", key);
    if (connect && server_ok && mqtt_connected)
    {
        if (en)
        {
            msg_id = esp_mqtt_client_publish(mqtt_client,
                                             topic_sensor,
                                             payload,
                                             0, 1, 1);
            send_data = true;
        }
        else
        {
            send_data = false;
            ESP_LOGW(TAG, "Server not allow");
        }
        if (msg_id == -1)
        {
            ESP_LOGW("MQTT", "Publish failed, saving to SPIFFS: %s", payload);
            save_to_spiffs(payload);
            return false;
        }
        ESP_LOGI("MQTT", "Publish queued msg_id=%d", msg_id);
        return true;
    }
    else
    {
        ESP_LOGW("MQTT", "No connection (wifi/server/mqtt), saving to SPIFFS: %s", payload);
        save_to_spiffs(payload);
        return false;
    }
}

void mqtt_publish_task() // test!!!
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    int last_index = (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;

    if (!isnan(oxygen_history[last_index]) &&
        !isnan(temp_history[last_index]))
    {
        char payload[200];
        int len = snprintf(payload, sizeof(payload),
                           "{\"time\":\"%s\","
                           "\"temperature\":%.2f,"
                           "\"oxygen\":%.2f,"
                           "\"do\":%.2f}",
                           time_history[last_index],
                           temp_history[last_index],
                           oxygen_history[last_index],
                           do_history[last_index]);

        if (len > 0)
        {
            ESP_LOGI(TAG, "Published payload: %s", payload);
            mqtt_safe_publish(payload);
        }
    }
    else
    {
        ESP_LOGW("MQTT", "No valid data to publish");
    }
}
void mqtt_spiffs()
{
    // iotlf/key/sensor/history
    char topic_history[64]; // topic for sensor history
    snprintf(topic_history, sizeof(topic_history), "iotlf/%s/sensor/history", key);
    FILE *f = fopen("/spiffs/unsend.txt", "r");
    if (!f)
    {
        ESP_LOGE("MQTT", "Failed to open file");
        vTaskDelete(NULL);
        return;
    }

    char line[256];
    char payload[4096];
    int count = 0;

    strcpy(payload, "[");
    while (fgets(line, sizeof(line), f))
    {
        line[strcspn(line, "\r\n")] = 0;

        if (strlen(payload) + strlen(line) + 2 >= sizeof(payload))
        {
            ESP_LOGE("MQTT", "Payload buffer overflow risk, flush early");
            strcat(payload, "]");
            if (mqtt_connected)
                esp_mqtt_client_publish(mqtt_client, topic_history, payload, 0, 1, 1);
            vTaskDelay(pdMS_TO_TICKS(500));
            strcpy(payload, "[");
            count = 0;
        }

        strcat(payload, line);
        count++;

        if (count < 50)
            strcat(payload, ",");

        if (count == 50)
        {
            size_t len = strlen(payload);
            if (payload[len - 1] == ',')
                payload[len - 1] = '\0';
            strcat(payload, "]");
            if (mqtt_connected)
                esp_mqtt_client_publish(mqtt_client, topic_history, payload, 0, 1, 1);
            vTaskDelay(pdMS_TO_TICKS(500));

            strcpy(payload, "[");
            count = 0;
        }
    }

    if (count > 0)
    {
        size_t len = strlen(payload);
        if (payload[len - 1] == ',')
            payload[len - 1] = '\0';
        strcat(payload, "]");
        if (mqtt_connected)
            esp_mqtt_client_publish(mqtt_client, topic_history, payload, 0, 1, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    fclose(f);
    ESP_LOGI("MQTT", "Finished sending history");
    vTaskDelete(NULL);
}
// do7019
// RYR404A
void mqtt_publish_ryr404a_task()
{
    char topic_ryr[64];
    char payload[128];
    // iotlf/key/relay/state/ryr404a/1,2,3,4
    for (size_t b = 0; b < RYR_NUM_BOARDS; ++b)
    {
        snprintf(topic_ryr, sizeof(topic_ryr), "iotlf/%s/relay/state/ryr404a/%u", key, (unsigned)(b + 1));
        int len = snprintf(payload, sizeof(payload),
                           "{\"board\":%u,\"ch\":[%u,%u,%u,%u]}",
                           (unsigned)(b + 1),
                           relay[b][0], relay[b][1], relay[b][2], relay[b][3]);

        if (len > 0 && mqtt_connected)
        {
            esp_mqtt_client_publish(mqtt_client, topic_ryr, payload, len, 1, 1);
            ESP_LOGI("MQTT", "Send to MQTT");
        }
    }
}

void mqtt_ryr404a_callback(const char *topic, const char *payload) // {"board":1,"ch":[0,1,0,1]}
{
    cJSON *root = cJSON_Parse(payload);
    if (!root)
    {
        ESP_LOGE("MQTT", "Invalid JSON: %s", payload);
        return;
    }

    cJSON *board = cJSON_GetObjectItem(root, "board");
    cJSON *ch = cJSON_GetObjectItem(root, "ch");

    if (!cJSON_IsNumber(board) || !cJSON_IsArray(ch))
    {
        ESP_LOGE("MQTT", "Invalid payload format");
        cJSON_Delete(root);
        return;
    }

    uint8_t slave_id = (uint8_t)board->valueint;

    uint8_t mask = 0;
    int arr_size = cJSON_GetArraySize(ch);
    for (int i = 0; i < arr_size && i < 8; i++)
    {
        cJSON *item = cJSON_GetArrayItem(ch, i);
        if (cJSON_IsNumber(item) && item->valueint)
        {
            mask |= (1 << i);
        }
    }
    ESP_LOGI("MQTT", "SlaveID=0x%02X, Mask=0x%02X", slave_id, mask);
    ESP_ERROR_CHECK(ryr_write_mask_board(slave_id, mask)); // function ryr404a_manage
    ryr404a_to_spiffs();                                   // function save to spiffs ryr404a_manage
    mqtt_publish_ryr404a_task();
    cJSON_Delete(root);
}
// RYR404A
// pca9557
void mqtt_publish_pca9557_task()
{
    char topic_pca[64];
    char payload[128];
    // iotlf/key/relay/state/pca9557
    snprintf(topic_pca, sizeof(topic_pca), "iotlf/%s/relay/state/pca9557", key);
    int len = snprintf(payload, sizeof(payload),
                       "{\"ch\":[%u,%u,%u,%u,%u,%u,%u,%u]}",
                       relay_state[0],
                       relay_state[1],
                       relay_state[2],
                       relay_state[3],
                       relay_state[4],
                       relay_state[5],
                       relay_state[6],
                       relay_state[7]);

    if (len > 0 && mqtt_connected)
    {
        esp_mqtt_client_publish(mqtt_client, topic_pca, payload, len, 1, 1);
        ESP_LOGI("MQTT", "Publish Relay State: %s", payload);
    }
}
void mqtt_pca9557_callback(const char *topic, const char *payload) //{"mask":15}
{
    cJSON *root = cJSON_Parse(payload);
    if (!root)
    {
        ESP_LOGE("MQTT", "Invalid JSON: %s", payload);
        return;
    }

    cJSON *mask_item = cJSON_GetObjectItem(root, "mask");
    if (!cJSON_IsNumber(mask_item))
    {
        ESP_LOGE("MQTT", "Invalid payload format");
        cJSON_Delete(root);
        return;
    }

    uint8_t mask = (uint8_t)mask_item->valueint;

    ESP_LOGI("MQTT", "Set Relay mask=0x%02X", mask);

    relay_set_mask(mask); // function pca9557_manage
    cJSON_Delete(root);
    pca9557_spiffs(); // funtion save to spiffs pca9557_manage
    mqtt_publish_pca9557_task();
}
void mqtt_task(void *pvParameters) // !!test
{
    ESP_LOGI(TAG, "MQTT Main Task start");
    while (!check_reg_device)
    {
        ESP_LOGW(TAG, "MQTT waiting reg data");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    ESP_LOGI(TAG, "MQTT recived reg data");
    mqtt_init();
    while (1)
    {
        mqtt_publish_task();
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
