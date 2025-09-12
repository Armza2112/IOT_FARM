#ifndef I2C_MANAGE_H
#define I2C_MANAGE_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/i2c.h"

#define I2C_MASTER_SCL_IO 22
#define I2C_MASTER_SDA_IO 21
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 100000
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_MASTER_RX_BUF_DISABLE 0

extern SemaphoreHandle_t i2c_mutex;
extern volatile bool i2c_busy ; 
extern bool load_wifi_screen;
void i2c_master_init(void);
void mutex_init(void);
void release_task_i2c(void);
void i2c_main_task();
#endif // I2C_MANAGE_H
