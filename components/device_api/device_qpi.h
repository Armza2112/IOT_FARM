#ifndef DEVICE_API_H
#define DEVICE_API_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_http_client.h"

#define UUID_STR_LEN 37

void load_uuid_from_spiffs(void);

void load_mqtt_credentials(void);

void save_mqtt_credentials(const char *user, const char *pass);

char *build_device_json(const char *macStr,
                        const char *ssid, const char *ip, int rssi,
                        int versionpro, bool boardstatus[4]);

void device_register_or_update(const char *ssid,
                               const char *ip,
                               int rssi,
                               int versionpro,
                               bool boardstatus[4]);

extern char uuidcid[UUID_STR_LEN];
extern char *mqttarr[2];

#endif // DEVICE_API_H
