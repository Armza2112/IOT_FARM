#ifndef MQTT_MANAGER_H
#define MQTT_MANAGER_H

#include <time.h> //test

extern bool mqtt_connected;
extern bool send_data ;

void mqtt_init();
void mqtt_publish_task(void *pvParameters);
void mqtt_spiffs();
void mqtt_publish_ryr404a_task();
void mqtt_publish_pca9557_task(void *pvParameters);
void mqtt_task();

#endif // WIFI_MANAGER_H
