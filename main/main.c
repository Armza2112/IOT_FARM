#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_spiffs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <driver/gpio.h>
#include "driver/i2c.h"
#include "../wifi_manager/wifi_manager.h"
#include "../web_server/web_server.h"
#include "../mqtt_manage/mqtt_manage.h"
#include "../uuid/uuid.h"
#include "../time_manage/time_manage.h"
#include "../device_api/device_qpi.h"
#include "../button_manage/button_manage.h"
#include "../i2c_manage/i2c_manage.h"
#include "../ryr404a_manage/ryr404a_manage.h"
#include "../do7019_manage/do7019_manage.h"
void init_spiffs()
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 10,
        .format_if_mount_failed = false};

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret != ESP_OK)
    {
        ESP_LOGE("SPIFFS", "Failed to mount or format filesystem");
    }
    else
    {
        ESP_LOGI("SPIFFS", "SPIFFS mounted successfully");
    }
}

void app_main(void)
{
    init_spiffs();
    ESP_ERROR_CHECK(modbus_master_init());
    wifi_init_apsta();
    connect_wifi_nvs();
    i2c_master_init();
    mutex_init();
    xTaskCreate(ryr404a_task, "ryr404a_task", 4096, NULL, 5, NULL);
    xTaskCreate(i2c_main_task, "i2c_main_task", 4096, NULL, 5, NULL);
    vTaskDelay(pdMS_TO_TICKS(60000));
    // initsettingsuuid();
    // load_uuid_from_spiffs();
    // device_register_or_update("MyWiFiSSID", "192.168.1.123", -55, 1, boardstatus);
    // vTaskDelay(pdMS_TO_TICKS(60000));

    // while (!time_ready)
    // {
    //     ESP_LOGW("Main", "Waiting for SNTP time sync...");
    //     vTaskDelay(pdMS_TO_TICKS(1000));
    // }

    // ESP_LOGI("Main", "Time ready, starting tasks...");

    xTaskCreate(do7019_task, "do7019_task", 4096, NULL, 5, NULL);
    mqtt_init();
    xTaskCreate(mqtt_task, "mqtt_task", 20480, NULL, 5, NULL);
    button_task();
}
