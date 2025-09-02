#ifndef DEVICE_API_H
#define DEVICE_API_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "esp_http_client.h"

// ขนาด UUID
#define UUID_STR_LEN 37

// โหลด UUID จาก SPIFFS
void load_uuid_from_spiffs(void);

// โหลด MQTT credentials จาก SPIFFS
void load_mqtt_credentials(void);

// เซฟ MQTT credentials ลง SPIFFS
void save_mqtt_credentials(const char *user, const char *pass);

// สร้าง JSON สำหรับ device
char *build_device_json(const char *macStr,
                        const char *ssid, const char *ip, int rssi,
                        int versionpro, bool boardstatus[4]);

// ฟังก์ชันหลัก: เช็ค, สมัคร หรือ อัพเดท device
void device_register_or_update(const char *ssid,
                               const char *ip,
                               int rssi,
                               int versionpro,
                               bool boardstatus[4]);

// ตัวแปร global
extern char uuidcid[UUID_STR_LEN];
extern char *mqttarr[2];

#endif // DEVICE_API_H
