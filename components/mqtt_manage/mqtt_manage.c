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
#include <math.h>
bool mqtt_connected = false; // flag mqtt connect

static const char *TAG_MQTT = "MQTT";
void mqtt_task();
// function
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
void mqtt_ryr404a_callback(const char *topic, const char *payload);
void mqtt_pca9557_callback(const char *topic, const char *payload);
void mqtt_reconnect_task();

// declare var
esp_mqtt_client_handle_t mqtt_client = NULL;

void mqtt_init()
{
    wifi_event_group = xEventGroupCreate();
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, false, true, portMAX_DELAY);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://broker.hivemq.com",
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
        ESP_LOGI(TAG_MQTT, "MQTT Connected");
        mqtt_connected = true;
        esp_mqtt_client_subscribe(event->client, "device/control/#", 2);
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        ESP_LOGI(TAG_MQTT, "MQTT Disconnected");
        xTaskCreate(mqtt_reconnect_task, "mqtt_reconnect_task", 1024, NULL, 5, NULL);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG_MQTT, "Message published (msg_id=%d)", event->msg_id);
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

        if (strcmp(topic, "device/control/ryr404a") == 0)
        {
            mqtt_ryr404a_callback(topic, payload);
        }
        else if (strcmp(topic, "device/control/pca9557") == 0)
        {
            while (i2c_busy)
            {
                ESP_LOGW("MQTT", "I2C busy, waiting...");
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            i2c_busy = true; // flag for i2c_manage
            mqtt_pca9557_callback(topic, payload);
            release_task_i2c(); // function release i2c_manage
            i2c_busy = false;   // flag for i2c_manage
        }

        free(payload);
        free(topic);
        break;
    }
    default:
        ESP_LOGI(TAG_MQTT, "Other MQTT event id:%ld", (long)event_id);
        break;
    }
}
void mqtt_reconnect_task()
{
    while (1)
    {
        if (!mqtt_connected && mqtt_client != NULL)
        {
            ESP_LOGW("MQTT", "Force restart MQTT client...");
            esp_mqtt_client_stop(mqtt_client);
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_mqtt_client_start(mqtt_client);
        }
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

// do7019
void mqtt_publish_task(void *pvParameters)
{
    TickType_t xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        int last_index = (history_index - 1 + HISTORY_SIZE) % HISTORY_SIZE;

        if (!isnan(oxygen_history[last_index]) &&
            !isnan(temp_history[last_index]))
        {
            char payload[200];
            int len = snprintf(payload, sizeof(payload),
                               "{\"time\":\"%s\","
                               "\"temperature\":%.2f,"
                               "\"oxygen\":%.2f,"
                               "\"salinity\":%.2f,"
                               "\"do\":%.2f}",
                               time_history[last_index],
                               temp_history[last_index],
                               oxygen_history[last_index],
                               salinity_history[last_index],
                               do_history[last_index]);

            if (len > 0)
            {
                ESP_LOGI(TAG_MQTT, "Published payload: %s", payload);
                esp_mqtt_client_publish(mqtt_client,
                                        "device/sensor/received",
                                        payload, 0, 1, 1);
            }
        }
        else
        {
            ESP_LOGW("MQTT", "No valid data to publish");
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(60000));
    }
}
void mqtt_spiffs(void *pvParameters)
{
    FILE *f = fopen("/spiffs/do7019.txt", "r");
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
                esp_mqtt_client_publish(mqtt_client, "device/sensor/history", payload, 0, 1, 1);
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
                esp_mqtt_client_publish(mqtt_client, "device/sensor/history", payload, 0, 1, 1);
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
            esp_mqtt_client_publish(mqtt_client, "device/sensor/history", payload, 0, 1, 1);
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    fclose(f);
    ESP_LOGI("MQTT", "Finished sending history");
    vTaskDelete(NULL);
}

// do7019
// RYR404A
void mqtt_publish_ryr404a_task(void *pvParameters)
{
    while (!mqtt_connected)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    char topic[64];
    char payload[128];
    while (1)
    {
        for (size_t b = 0; b < RYR_NUM_BOARDS; ++b)
        {
            snprintf(topic, sizeof(topic), "device/relay/ryr404a/%u", (unsigned)(b + 1));
            int len = snprintf(payload, sizeof(payload),
                               "{\"board\":%u,\"ch\":[%u,%u,%u,%u]}",
                               (unsigned)(b + 1),
                               relay[b][0], relay[b][1], relay[b][2], relay[b][3]);

            if (len > 0 && mqtt_connected)
            {
                esp_mqtt_client_publish(mqtt_client, topic, payload, len, 1, 1);
                ESP_LOGI("MQTT", "Send to MQTT");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
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
    cJSON_Delete(root);
}
// RYR404A
// pca9557
void mqtt_publish_pca9557_task(void *pvParameters)
{
    while (!mqtt_connected)
    {
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    char topic[64];
    char payload[128];

    while (1)
    {
        snprintf(topic, sizeof(topic), "device/relay/pca9557");

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
            esp_mqtt_client_publish(mqtt_client, topic, payload, len, 1, 1);
            ESP_LOGI("MQTT", "Publish Relay State: %s", payload);
        }

        vTaskDelay(pdMS_TO_TICKS(60000));
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

    vTaskDelay(pdMS_TO_TICKS(500));
}
void mqtt_task()
{
    while (!mqtt_connected)
    {
        ESP_LOGI("MQTT", "Waiting for MQTT connection...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI("MQTT", "Starting MQTT publish tasks...");

    xTaskCreate(mqtt_spiffs, "mqtt_spiffs", 8192, NULL, 5, NULL);
    xTaskCreate(mqtt_publish_ryr404a_task, "mqtt_publish_ryr404a_task", 4096, NULL, 5, NULL);
    xTaskCreate(mqtt_publish_pca9557_task, "mqtt_publish_pca9557_task", 4096, NULL, 5, NULL);
    xTaskCreate(mqtt_publish_task, "mqtt_publish_task", 4096, NULL, 5, NULL);

    vTaskDelete(NULL);
}