#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>
#include <u8g2.h>
#include "image_hex.h"
#include "u8g2_esp32_hal.h"
#include "qrcode.h"
#include <esp_wifi.h>
#include <time.h>
#include "../data_esp32/data_esp32.h"
#include "../button_manage/button_manage.h"
#include "../time_manage/time_manage.h"
#include "../i2c_manage/i2c_manage.h"
#include "../mqtt_manage/mqtt_manage.h"
#include "../pca9557_manage/pca9557_manage.h"
#include "../device_api/device_api.h"

#define PIN_SDA 21
#define PIN_SCL 22

// Flags
bool blink_state = false;
bool send_data = false;
bool error_message = true;

// OLED
u8g2_t u8g2;
u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;

static const char *TAG = "OLED";

void oled_init()
{
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)))
    {
        u8g2_esp32_hal.bus.i2c.sda = PIN_SDA;
        u8g2_esp32_hal.bus.i2c.scl = PIN_SCL;
        u8g2_esp32_hal_init(u8g2_esp32_hal);
        u8g2_Setup_sh1106_i2c_128x64_noname_f(&u8g2, U8G2_R0, u8g2_esp32_i2c_byte_cb, u8g2_esp32_gpio_and_delay_cb);
        u8x8_SetI2CAddress(&u8g2.u8x8, 0x78);
        ESP_LOGI(TAG, "u8g2_InitDisplay");
        u8g2_InitDisplay(&u8g2);
        ESP_LOGI(TAG, "u8g2_SetPowerSave");
        u8g2_SetPowerSave(&u8g2, 0);
        ESP_LOGI(TAG, "u8g2_ClearBuffer");
        u8g2_ClearBuffer(&u8g2);
        xSemaphoreGive(i2c_mutex);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to take I2C mutex for oled_init");
    }
}

void oled_center_text(const char *text, int y)
{
    int str_width = u8g2_GetStrWidth(&u8g2, text);
    int center_x = (128 - str_width) / 2;
    u8g2_DrawStr(&u8g2, center_x, y, text);
}

void draw_startup()
{
    const char *msg[4] = {"Checking WiFi", "Time sync", "Waiting", "Starting"};
    char buf[32];

    for (int i = 0; i < 4; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            snprintf(buf, sizeof(buf), "%s%.*s", msg[i], j + 1, "....");
            if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)))
            {
                u8g2_ClearBuffer(&u8g2);
                u8g2_DrawXBM(&u8g2, -2, 10, 128, 30, logo);
                u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
                oled_center_text(buf, 60);
                u8g2_SendBuffer(&u8g2);
                xSemaphoreGive(i2c_mutex);
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void draw_wifi_screen()
{
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(2000)))
    {
        u8g2_ClearBuffer(&u8g2);
        u8g2_SetFont(&u8g2, u8g2_font_timB08_tr);
        oled_center_text("WIFI MANAGE", 8);

        QRCode qrcode;
        uint8_t qrcode_data[qrcode_getBufferSize(3)];
        qrcode_initText(&qrcode, qrcode_data, 3, ECC_LOW, "http://192.168.4.1/");

        int offset_x = 2, offset_y = 13;
        float scale_f = 50.0f / qrcode.size;

        for (int y = 0; y < qrcode.size; y++)
        {
            for (int x = 0; x < qrcode.size; x++)
            {
                if (qrcode_getModule(&qrcode, x, y))
                {
                    int px = offset_x + (int)(x * scale_f);
                    int py = offset_y + (int)(y * scale_f);
                    int box_size = (int)(scale_f + 0.5f);
                    u8g2_DrawBox(&u8g2, px, py, box_size, box_size);
                }
            }
        }

        u8g2_SetFont(&u8g2, u8g2_font_4x6_tr);
        u8g2_DrawStr(&u8g2, 58, 20, "1.CONNECT WIFI");
        u8g2_DrawStr(&u8g2, 66, 28, "ESP32");
        u8g2_DrawStr(&u8g2, 58, 36, "SSID:ESP32");
        u8g2_DrawStr(&u8g2, 58, 44, "Password:1234");
        u8g2_DrawStr(&u8g2, 58, 52, "2.SCAN QR CODE");
        u8g2_DrawStr(&u8g2, 58, 60, "3.SET WIFI ON WEB");

        u8g2_SendBuffer(&u8g2);
        xSemaphoreGive(i2c_mutex);
    }
}

void draw_main_screen()
{

    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(2000)))
    {
        u8g2_ClearBuffer(&u8g2);
        // WiFi icon
        int rssi;
        char rssi_str[4];
        get_sta_rssi(&rssi, rssi_str, sizeof(rssi_str));
        wifi_ap_record_t ap_info;
        bool connect = (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK);
        if (connect)
        {
            if (rssi >= -50)
                u8g2_DrawXBM(&u8g2, 116, 0, 10, 10, wifi_4_icon);
            else if (rssi >= -65)
                u8g2_DrawXBM(&u8g2, 116, 0, 10, 10, wifi_3_icon);
            else if (rssi >= -80)
                u8g2_DrawXBM(&u8g2, 116, 0, 10, 10, wifi_2_icon);
            else
                u8g2_DrawXBM(&u8g2, 116, 0, 10, 10, wifi_1_icon);
        }
        else if (blink_state)
        {
            u8g2_DrawXBM(&u8g2, 116, 0, 10, 10, wifi_unconnect_icon);
        }

        // RTC
        struct tm ds_time = {0};
        ds_time.tm_hour = hour_ds1307; // read time form ds1307
        ds_time.tm_min = min_ds1307;
        char time_str[7];
        strftime(time_str, sizeof(time_str), "%H:%M", &ds_time);
        u8g2_SetFont(&u8g2, u8g2_font_5x8_tn);
        u8g2_DrawStr(&u8g2, 0, 7, time_str);

        // MQTT
        if (mqtt_connected)
            u8g2_DrawXBM(&u8g2, 100, 0, 10, 10, mqtt_icon);
        else if (blink_state)
            u8g2_DrawXBM(&u8g2, 97, 0, 10, 10, mqtt_icon);

        // Sensor icons & data
        u8g2_SetFont(&u8g2, u8g2_font_5x8_tn);
        u8g2_DrawXBM(&u8g2, 12, 17, 15, 15, temp_icon);
        u8g2_DrawStr(&u8g2, 32, 28, "32");
        u8g2_DrawXBM(&u8g2, 62, 17, 15, 15, o2_icon);
        u8g2_DrawStr(&u8g2, 84, 28, "25");
        u8g2_DrawXBM(&u8g2, 10, 39, 15, 15, salinity_icon);
        u8g2_DrawStr(&u8g2, 31, 50, "24");
        u8g2_DrawXBM(&u8g2, 62, 39, 15, 15, do_icon);
        u8g2_DrawStr(&u8g2, 84, 50, "24");
        u8g2_SetFont(&u8g2, u8g2_font_4x6_tr);
        u8g2_DrawStr(&u8g2, 44, 27, "C");
        u8g2_DrawStr(&u8g2, 97, 28, "mg/L");
        u8g2_DrawStr(&u8g2, 42, 50, "%");
        u8g2_DrawStr(&u8g2, 97, 50, "g/L");

        if (send_data || blink_state)
            u8g2_DrawXBM(&u8g2, 77, 0, 10, 10, data_icon);
        if (server_connected || blink_state)
            u8g2_DrawXBM(&u8g2, 57, 0, 10, 10, server_icon);
        if (error_message && blink_state)
            u8g2_SetFont(&u8g2, u8g2_font_6x10_tr);
        oled_center_text("Error message: 2", 64);

        u8g2_SendBuffer(&u8g2);
        xSemaphoreGive(i2c_mutex);
    }
    ESP_LOGI("OLED", "OLED DONE");
}
