#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "../wifi_manager/wifi_manager.h"
#include "../web_server/web_server.h"
#include "../oled/oled_manage.h"

#define MODE_BUTTON GPIO_NUM_0
bool show_wifi_screen = false;

static esp_timer_handle_t long_press_timer;

void IRAM_ATTR button_isr_handler(void *arg)
{
    int level = gpio_get_level(MODE_BUTTON);
    if (level == 0)
    {
        esp_timer_start_once(long_press_timer, 5000000); // 5 sec
    }
    else
    {
        esp_timer_stop(long_press_timer);
    }
}
static TaskHandle_t webserver_task_handle = NULL;

void webserver_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(2000));
    show_wifi_screen=true;
    esp_netif_ip_info_t ip_info;
    esp_netif_t* ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_get_ip_info(ap_netif, &ip_info);
    ESP_LOGI("WEB", "Starting webserver at IP: " IPSTR, IP2STR(&ip_info.ip));
    start_webserver(); 
    vTaskDelete(NULL);
}
void button_handler_task(void *arg)
{
    ESP_LOGI("BUTTON", "Init AP+STA");
    esp_netif_ip_info_t ip_info;
    esp_netif_t *ap_netif = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_get_ip_info(ap_netif, &ip_info);
    ESP_LOGI("WEB", "SoftAP IP: " IPSTR, IP2STR(&ip_info.ip));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    vTaskDelay(pdMS_TO_TICKS(2000)); // รอ STA/AP ready
    xTaskCreate(webserver_task, "webserver_task", 8192, NULL, 5, NULL);
    xTaskCreate(wifi_scan_task, "wifi_scan_task", 8192, NULL, 5, NULL);
    
    vTaskDelete(NULL);
}
void button_task_handler(void *arg)
{
    xTaskCreate(button_handler_task, "button_handler_task", 8192, NULL, 5, NULL);
}
void button_task()
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MODE_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE};
    gpio_config(&io_conf);

    esp_timer_create_args_t timer_args = {
        .callback = button_task_handler,
        .arg = NULL,
        .name = "wifi_init_apsta_timer_cb"};
    esp_timer_create(&timer_args, &long_press_timer);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(MODE_BUTTON, button_isr_handler, NULL);
}
