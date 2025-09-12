#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include <stdbool.h>
#include <stdint.h>

#define MAXIMUM_AP 20
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_DISCONNECTED_BIT BIT1

extern wifi_ap_record_t scanned_aps[MAXIMUM_AP]; 
extern int16_t scanned_ap_count;
extern TaskHandle_t main_task_handle;
extern EventGroupHandle_t wifi_event_group;

extern bool user_disconnect;
extern bool wrong_password;
extern bool wifi_unconnect;
extern bool test;
void wifi_connect(const char *ssid, const char *password);
void wifi_init_apsta(void);
void wifi_scan_task(void *pvParameters);
void connect_wifi_nvs(void);


#endif // WIFI_MANAGER_H
