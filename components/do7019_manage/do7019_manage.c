#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <time.h>
#include <string.h>
#include "esp_spiffs.h"
#include "../time_manage/time_manage.h"
#include "esp_log.h"
#define HISTORY_SIZE 100
#define TIME_STR_LEN 6
#define MAX_RECORDS_PER_DAY 1440

static int do7019_record_count;
float temp_history[HISTORY_SIZE] = {NAN};
float oxygen_history[HISTORY_SIZE] = {NAN};
float salinity_history[HISTORY_SIZE] = {NAN};
float do_history[HISTORY_SIZE] = {NAN};

char time_history[HISTORY_SIZE][TIME_STR_LEN];
int history_index = 0;

void do7019_spiffs(float temp, float oxygen, float sanility, float do_value, const char *time_str);

int count_lines_in_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
    {
        ESP_LOGW("DO7019", "File not found, count = 0");
        return 0;
    }

    int count = 0;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        count++;
    }

    fclose(f);
    return count;
}
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
void do7019_task()
{
    char time_str[6];

    while (1)
    {
        snprintf(time_str, sizeof(time_str), "%02d:%02d", hour_ds1307, min_ds1307);
        do7019_spiffs(30.0, 20, 1, 20, time_str);      // test value
        add_sensor_reading(30.0, 20, 1, 20, time_str); // test value
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}

void do7019_spiffs(float temp, float oxygen, float sanility, float do_value, const char *time_str)
{
    const char *mode = "a";
    do7019_record_count = count_lines_in_file("/spiffs/do7019.txt");
    if (do7019_record_count >= MAX_RECORDS_PER_DAY) // if over 1440 reset file
    {
        ESP_LOGW("DO7019", "Reached %d records, resetting file", MAX_RECORDS_PER_DAY);
        mode = "w";
        do7019_record_count = 0;
    }

    FILE *f = fopen("/spiffs/do7019.txt", mode);
    if (f)
    {
        fprintf(f,
                "{\"time\":\"%s\",\"temp\":%.2f,\"o2\":%.2f,\"sal\":%.2f,\"do\":%.2f}\n",
                time_str, temp, oxygen, sanility, do_value);
        fclose(f);
        ESP_LOGI("DO7019", "Save to spiffs: %s (count=%d)", time_str, do7019_record_count);
    }
    else
    {
        ESP_LOGE("DO7019", "Failed to open file for writing");
    }
}