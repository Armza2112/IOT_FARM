#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <driver/gpio.h>
#include "driver/i2c.h"
#include "../time_manage/time_manage.h"
#include "../oled/oled_manage.h"
#include "../pca9557_manage/pca9557_manage.h"
#include "../button_manage/button_manage.h"

#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0
#define I2C_MASTER_NUM I2C_NUM_0

SemaphoreHandle_t i2c_mutex;
// flags
volatile bool i2c_busy = false;

void i2c_master_init()
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,

    };
    i2c_param_config(I2C_MASTER_NUM, &conf);
    i2c_driver_install(I2C_MASTER_NUM, conf.mode,
                       I2C_MASTER_RX_BUF_DISABLE,
                       I2C_MASTER_TX_BUF_DISABLE, 0);
    i2c_set_timeout(I2C_NUM_0, 800000);
}
void mutex_init()
{
    i2c_mutex = xSemaphoreCreateMutex();
    if (i2c_mutex == NULL)
    {
        ESP_LOGE("MUTEX", "Failed to create I2C mutex");
    }
    else
    {
        ESP_LOGI("MUTEX", "I2C mutex created successfully");
    }
}
void release_task_i2c()
{
    vTaskDelay(pdMS_TO_TICKS(100));
    i2c_driver_delete(I2C_MASTER_NUM);

    gpio_set_direction(I2C_MASTER_SDA_IO, GPIO_MODE_INPUT_OUTPUT_OD);
    gpio_set_direction(I2C_MASTER_SCL_IO, GPIO_MODE_INPUT_OUTPUT_OD);

    for (int i = 0; i < 9; i++)
    {
        gpio_set_level(I2C_MASTER_SCL_IO, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(I2C_MASTER_SCL_IO, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    gpio_set_level(I2C_MASTER_SDA_IO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(I2C_MASTER_SCL_IO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(I2C_MASTER_SDA_IO, 1);

    i2c_master_init();
    vTaskDelay(pdMS_TO_TICKS(100));
}

void safe_i2c_action(void (*action)(void))
{
    while (i2c_busy)
    {
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    i2c_busy = true;
    action();
    release_task_i2c();
    i2c_busy = false;
}

void i2c_main_task()
{
    int counter = 0;
    oled_init();
    draw_startup();
    vTaskDelay(pdMS_TO_TICKS(4000));
    release_task_i2c();
    sntp_ds1307_task();
    ESP_ERROR_CHECK(pca9557_init_once());
    release_task_i2c();
    while (1)
    {
        blink_state = !blink_state;
        if (show_wifi_screen)
        {
            safe_i2c_action(draw_wifi_screen);
            ESP_LOGI("I2C_FLAG", "After draw_wifi_screen: i2c_busy=%s", i2c_busy ? "true" : "false");
        }
        else
        {

            safe_i2c_action(draw_main_screen);
            ESP_LOGI("I2C_FLAG", "After draw_main_screen: i2c_busy=%s", i2c_busy ? "true" : "false");
        }

        safe_i2c_action(ds1307_get_time);
        ESP_LOGI("I2C_FLAG", "After ds1307_get_time: i2c_busy=%s", i2c_busy ? "true" : "false");

        vTaskDelay(pdMS_TO_TICKS(1000));

        if (counter % 60 == 0)
        {
            safe_i2c_action(pca9557_task);
            ESP_LOGI("I2C_FLAG", "After pca9557_task: i2c_busy=%s", i2c_busy ? "true" : "false");
        }

        counter++;
    }
}