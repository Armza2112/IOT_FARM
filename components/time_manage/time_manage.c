#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <time.h>
#include "esp_log.h"
#include "lwip/apps/sntp.h"
#include "driver/i2c.h"
#include "esp_wifi.h"
#include "../i2c_manage/i2c_manage.h"
#include "../device_api/device_api.h"
#define DS1307_ADDR 0x68

int hour_ds1307;
int min_ds1307;
char date_ds1307[4];
bool time_ready = false;
static bool time_set = false;
bool time_correct = false;
static const char *TAG = "DS1307";
static bool already_synced = false;
static bool already_check = false;
bool already_check_time = false;
bool parse_iso8601(const char *iso_str, struct tm *tm_out)
{
    memset(tm_out, 0, sizeof(struct tm));
    if (strptime(iso_str, "%Y-%m-%dT%H:%M:%SZ", tm_out) == NULL)
    {
        return false;
    }
    return true;
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
void save_state_set_time_spiffs()
{
    FILE *f = fopen("/spiffs/sat_t.txt", "r");
    if (f == NULL)
    {
        if (strlen(device_time) > 0)
        {
            ESP_LOGE(TAG, "Failed to open file for reading");
            int state_set_time = 1;
            FILE *f = fopen("/spiffs/sat_t.txt", "w");
            if (f == NULL)
            {
                ESP_LOGE(TAG, "Failed to create sat_t.txt");
                return;
            }
            fprintf(f, "%d", state_set_time);
            ESP_LOGI(TAG, "Saved: %d", state_set_time);
            fclose(f);
            time_set = false;
        }
        else
        {
            ESP_LOGW(TAG, "Cant recived device_time form server");
        }
    }
    else
    {
        ESP_LOGI(TAG, "sat_t.txt already exists, not overwritten");
        fclose(f);
        time_set = true;
    }
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
    snprintf(date_ds1307, sizeof(date_ds1307), "%02d", date);
}
bool check_time_with_ds1307()
{
    struct tm tm_server;
    if (!parse_iso8601(device_time, &tm_server))
    {
        return false;
    }

    time_t t_server = mktime(&tm_server);

    uint8_t data[7];
    ds1307_safe_read(0x00, data, sizeof(data));

    struct tm tm_ds;
    memset(&tm_ds, 0, sizeof(tm_ds));
    tm_ds.tm_sec = bcd_to_dec(data[0] & 0x7F);
    tm_ds.tm_min = bcd_to_dec(data[1]);
    tm_ds.tm_hour = bcd_to_dec(data[2] & 0x3F);
    tm_ds.tm_mday = bcd_to_dec(data[4]);
    tm_ds.tm_mon = bcd_to_dec(data[5]) - 1;
    tm_ds.tm_year = bcd_to_dec(data[6]) + 100;

    time_t t_ds = mktime(&tm_ds);

    double diff = difftime(t_server, t_ds);

    printf("Server time: %s", asctime(&tm_server));
    printf("DS1307 time: %s", asctime(&tm_ds));
    printf("Difference: %.0f sec\n", diff);

    return (diff >= -60 && diff <= 60);
}
void ds1307_task()
{
    ESP_LOGI(TAG, "DS1307 Main Task start");
    wifi_ap_record_t ap_info;
    bool connect = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
    if (connect) // if connect run task else if dont set time and. dont run task
    {
        // if sever alive run task else if server dead dont set time and dont run task
        if (check_server_alive())
        {
            save_state_set_time_spiffs();
            // check ever set time and ever sync from spiffs
            if (time_set)
            {
                // check is ever check time with server?
                if (!already_check)
                {
                    // loop check device time with server is already? (device_api)
                    while (!check_time_device)
                    {
                        ESP_LOGW(TAG, "Waiting time_device");
                        vTaskDelay(pdMS_TO_TICKS(5000));
                    }
                    // check time(ds_1307) with device time(server) is match?
                    if (check_time_with_ds1307())
                    {
                        time_correct = true;
                    }
                    already_check = true;
                    post_time_set();           // send check time to http(device_api.c)
                    check_time_device = false; // set check time(device_time) to default(device_api)
                }
                else
                {
                    ESP_LOGI(TAG, "DS1307 Main Task done");
                }
            }
            // if time never set on spiffs do set
            else
            {
                struct tm timeinfo;
                if (!parse_iso8601(device_time, &timeinfo))
                {
                    ESP_LOGE(TAG, "Failed to parse device_time: %s", device_time);
                    return;
                }
                ESP_LOGI(TAG, "Parsed time: %04d-%02d-%02d %02d:%02d:%02d (wday=%d)",
                         timeinfo.tm_year + 1900,
                         timeinfo.tm_mon + 1,
                         timeinfo.tm_mday,
                         timeinfo.tm_hour,
                         timeinfo.tm_min,
                         timeinfo.tm_sec,
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
                time_correct = true;
                already_check_time = true;
                already_check = false;
            }
            ESP_LOGI(TAG, "DS1307 Main Task done");
        }
        else
        {
            ESP_LOGW(TAG, "Cant do about time time (disconnect. server)");
            already_check = false;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Cant do about time time (disconnect_wifi)");
        already_check = false;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
