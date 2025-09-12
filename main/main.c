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
#include "../time_manage/time_manage.h"
#include "../device_api/device_api.h"
#include "../button_manage/button_manage.h"
#include "../i2c_manage/i2c_manage.h"
#include "../ryr404a_manage/ryr404a_manage.h"
#include "../do7019_manage/do7019_manage.h"
#include "../data_esp32/data_esp32.h"
#include <dirent.h>
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
    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("/spiffs")) != NULL)
    {
        while ((ent = readdir(dir)) != NULL)
        {
            ESP_LOGI("SPIFFS", "Found file: %s", ent->d_name);
        }
        closedir(dir);
    }
    else
    {
        ESP_LOGE("SPIFFS", "Failed to open /spiffs");
    }
    load_data_task();
    ESP_ERROR_CHECK(modbus_master_init());
    button_task();
    wifi_init_apsta();
    connect_wifi_nvs();
    i2c_master_init();
    mutex_init();
    xTaskCreate(mqtt_task, "mqtt_task", 30000, NULL, 5, NULL);
    xTaskCreate(http_task, "http_task", 8192, NULL, 5, NULL);
    xTaskCreate(ryr404a_task, "ryr404a_task", 4096, NULL, 5, NULL);
    xTaskCreate(i2c_main_task, "i2c_main_task", 8192, NULL, 5, NULL);
    xTaskCreate(do7019_task, "do7019_task", 4096, NULL, 5, NULL);
    // mqtt_init();
}