#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    DS1307_CMD_SET_BOOT_TIME
} ds1307_cmd_t;

extern QueueHandle_t ds1307_queue;
extern int hour_ds1307;
extern int min_ds1307;
extern bool time_ready;
extern bool time_correct ;
extern char date_ds1307[3];
extern bool already_check_time;
void ds1307_task();
void ds1307_get_time();
void initialize_sntp();
