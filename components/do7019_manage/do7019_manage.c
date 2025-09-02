#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include "esp_spiffs.h"
#include "../time_manage/time_manage.h"

#define HISTORY_SIZE 100
#define TIME_STR_LEN 6
float temp_history[HISTORY_SIZE] = {NAN};
float oxygen_history[HISTORY_SIZE] = {NAN};
float salinity_history[HISTORY_SIZE] = {NAN};
float do_history[HISTORY_SIZE] = {NAN};

char time_history[HISTORY_SIZE][TIME_STR_LEN];
int history_index = 0;

void add_sensor_reading(float temp, float oxygen, float sanility, float do_value, const char *time_str)
{
    temp_history[history_index] = temp;
    oxygen_history[history_index] = oxygen;
    salinity_history[history_index] = sanility;
    do_history[history_index] = do_value;

    strncpy(time_history[history_index], time_str, TIME_STR_LEN - 1);
    time_history[history_index][TIME_STR_LEN - 1] = '\0';
    history_index = (history_index + 1) % HISTORY_SIZE;
}
void read_do7019()
{
    char time_str[6];
    while (1)
    {
        snprintf(time_str, sizeof(time_str), "%02d:%02d", hour_ds1307, min_ds1307);
        do7019_spiffs(30.0, 20, 1, 20, time_str);
        add_sensor_reading(30.0, 20, 1, 20, time_str);
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
void do7019_spiffs(float temp, float oxygen, float sanility, float do_value, const char *time_str)
{
    FILE *f = fopen("/spiffs/do7019.txt", "w");
    if (f)
    {
        fprintf(f, "{\"time\":\"%s\",\"temp\":%.2f,\"o2\":%.2f,\"sal\":%.2f,\"do\":%.2f}\n", time_str, temp, oxygen, sanility, do_value);
        fclose(f);
        ESP_LOGI("DO7019", "Save to spiffs");
    }
}
void do7019_task()
{
    read_do7019();
    vTaskDelete(NULL);
}