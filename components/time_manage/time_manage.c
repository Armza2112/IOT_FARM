#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <time.h> 
#include "esp_log.h"
#include "lwip/apps/sntp.h"
#include "driver/i2c.h"
#include "../i2c_manage/i2c_manage.h"
#define DS1307_ADDR 0x68

int hour_ds1307 ;
int min_ds1307 ;

static const char *TAG = "DS1307";

void initialize_sntp()
{
    ESP_LOGI("SNTP", "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    setenv("TZ", "GMT-7", 1);
    tzset();
    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 10;

    while (timeinfo.tm_year < (2020 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI("SNTP", "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (retry == retry_count)
    {
        ESP_LOGW("SNTP", "Failed to sync time with NTP");
    }
    else
    {
        ESP_LOGI("SNTP", "Time synchronized: %s", asctime(&timeinfo));
    }
}

void stop_sntp()
{
    sntp_stop();
    ESP_LOGI("SNTP", "Stop SNTP");
}
uint8_t dec_to_bcd(uint8_t val)
{
    return ((val / 10) << 4) + (val % 10); // val=25 -> 0000 0010 -> 0010 0000 -> 0010 0101
}
uint8_t bcd_to_dec(uint8_t val)
{
    return (val >> 4) * 10 + (val & 0x0f); // val=25 -> 0000 0010 -> 0010 0000 -> 0010 0101
}
esp_err_t ds1307_safe_write(uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(2000)))
    {
        ESP_LOGI(TAG, "Before DS1307 write");
        ret = i2c_master_write_to_device(I2C_MASTER_NUM, DS1307_ADDR, data, len, 3000 / portTICK_PERIOD_MS);
        vTaskDelay(pdMS_TO_TICKS(5));
        ESP_LOGI(TAG, "After DS1307 write, ret=%d", ret);
        xSemaphoreGive(i2c_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to take I2C mutex for write");
    }
    return ret;
}

esp_err_t ds1307_safe_read(uint8_t reg, uint8_t *data, size_t len)
{
    esp_err_t ret = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)))
    {
        ret = i2c_master_write_read_device(I2C_MASTER_NUM, DS1307_ADDR, &reg, 1, data, len, 3000 / portTICK_PERIOD_MS);
        xSemaphoreGive(i2c_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Failed to take I2C mutex for read");
    }
    return ret;
}

void ds1307_set_time(uint8_t sec, uint8_t min, uint8_t hour,
                     uint8_t day, uint8_t date, uint8_t month, uint8_t year)
{
    uint8_t data[8];
    data[0] = 0x00;
    data[1] = dec_to_bcd(sec & 0x7F); // reg 0x00
    data[2] = dec_to_bcd(min);
    data[3] = dec_to_bcd(hour);
    data[4] = dec_to_bcd(day);
    data[5] = dec_to_bcd(date);
    data[6] = dec_to_bcd(month);
    data[7] = dec_to_bcd(year);
    ds1307_safe_write(data, sizeof(data));
    vTaskDelay(pdMS_TO_TICKS(10));
}

void ds1307_get_time()
{
    uint8_t reg = 0x00;
    uint8_t data[7];

    ds1307_safe_read(reg, data, sizeof(data));

    uint8_t sec = bcd_to_dec(data[0] & 0x7F);
    uint8_t min = bcd_to_dec(data[1]);
    uint8_t hour = bcd_to_dec(data[2] & 0x3F);
    uint8_t day = bcd_to_dec(data[3]);
    uint8_t date = bcd_to_dec(data[4]);
    uint8_t month = bcd_to_dec(data[5]);
    uint8_t year = bcd_to_dec(data[6]);

    ESP_LOGI(TAG, "Time: %02d:%02d:%02d Date: %02d/%02d/20%02d (Day:%d)",
             hour, min, sec, date, month, year, day);

    hour_ds1307 = hour;
    min_ds1307 = min;
}
void sntp_ds1307_task()
{
    ESP_LOGI(TAG, "DS1307 Task start");

    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    vTaskDelay(pdMS_TO_TICKS(200));
    ESP_LOGI(TAG, "Setting DS1307: %02d:%02d:%02d %02d/%02d/20%02d Day:%d",
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
             timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year - 100,
             timeinfo.tm_wday + 1);
    ds1307_set_time(
        timeinfo.tm_sec,
        timeinfo.tm_min,
        timeinfo.tm_hour,
        timeinfo.tm_wday + 1,
        timeinfo.tm_mday,
        timeinfo.tm_mon + 1,
        timeinfo.tm_year - 100);
    ds1307_get_time();
    ESP_LOGI(TAG, "DS1307 Task done");
}
