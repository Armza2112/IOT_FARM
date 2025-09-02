#include <stdio.h>
#include <stdbool.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/semphr.h"
#include "../i2c_manage/i2c_manage.h"
#include "esp_spiffs.h"
#include <string.h>

#define PCA9557_ADDR 0x18
#define PCA9557_REG_INPUT 0x00
#define PCA9557_REG_OUTPUT 0x01
#define PCA9557_REG_POLINV 0x02
#define PCA9557_REG_CONFIG 0x03
#define PCA9557_NUM_CHANNELS 8
#define RELAY_ACTIVE_LOW_MASK 0xFF // 0b11111111 active low

uint8_t relay_state[PCA9557_NUM_CHANNELS] = {0}; //  ON/OFF
void relay_load_from_spiffs(void);

static const char *TAG = "PCA9557";

static inline uint8_t logical_to_raw(uint8_t logical)
{
    return (uint8_t)(logical ^ RELAY_ACTIVE_LOW_MASK);
}
uint8_t raw_to_logical(uint8_t raw)
{
    return (uint8_t)(raw ^ RELAY_ACTIVE_LOW_MASK);
}
static esp_err_t pca9557_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    esp_err_t ret = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)))
    {
        ret = i2c_master_write_to_device(I2C_MASTER_NUM, PCA9557_ADDR, buf, 2, 1000 / portTICK_PERIOD_MS);
        xSemaphoreGive(i2c_mutex);
    }
    return ret;
}

esp_err_t pca9557_read_reg(uint8_t reg, uint8_t *val)
{
    esp_err_t ret = ESP_ERR_TIMEOUT;
    if (xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(1000)))
    {
        ret = i2c_master_write_read_device(I2C_MASTER_NUM, PCA9557_ADDR, &reg, 1, val, 1, 1000 / portTICK_PERIOD_MS);
        xSemaphoreGive(i2c_mutex);
    }
    return ret;
}

esp_err_t pca9557_init_once(void)
{
    ESP_ERROR_CHECK(pca9557_write_reg(PCA9557_REG_CONFIG, 0x00));

    relay_load_from_spiffs();

    ESP_LOGI(TAG, "PCA9557 initialized (restore last state if available)");
    return ESP_OK;
}

esp_err_t relay_write_mask(uint8_t mask)
{
    return pca9557_write_reg(PCA9557_REG_OUTPUT, logical_to_raw(mask));
}

esp_err_t relay_get(int ch, bool *on_out)
{
    if (!on_out || ch < 1 || ch > 8)
        return ESP_ERR_INVALID_ARG;

    uint8_t raw = 0;
    esp_err_t err = pca9557_read_reg(PCA9557_REG_OUTPUT, &raw);
    if (err != ESP_OK)
        return err;

    uint8_t logical = raw_to_logical(raw);
    *on_out = ((logical >> (ch - 1)) & 1u) != 0;
    return ESP_OK;
}
void update_relay_status(void)
{
    uint8_t raw = 0;
    if (pca9557_read_reg(PCA9557_REG_OUTPUT, &raw) == ESP_OK)
    {
        uint8_t logical = raw_to_logical(raw);

        for (int i = 0; i < PCA9557_NUM_CHANNELS; i++)
        {
            relay_state[i] = (logical >> i) & 1;
        }

        ESP_LOGI("PCA9557", "Relay status updated: 0x%02X", logical);
    }
    else
    {
        ESP_LOGW("PCA9557", "Failed to read relay state");
    }
}
void relay_set_mask(uint8_t mask)
{

    esp_err_t err = pca9557_write_reg(PCA9557_REG_OUTPUT, logical_to_raw(mask));

    if (err == ESP_OK)
    {

        for (int i = 0; i < PCA9557_NUM_CHANNELS; i++)
        {
            relay_state[i] = (mask >> i) & 1;
        }

        ESP_LOGI("PCA9557", "Relay updated, mask=0x%02X", mask);
    }
    else
    {
        ESP_LOGE("PCA9557", "Failed to write relay mask, err=0x%x", err);
    }
}
void pca9557_spiffs(void)
{
    const char *path = "/spiffs/relay_pca9557.csv";
    FILE *f = fopen(path, "w");
    if (!f)
    {
        ESP_LOGE("PCA9557", "Failed to open %s for writing", path);
        return;
    }

    uint8_t mask = 0;
    for (int i = 0; i < PCA9557_NUM_CHANNELS; i++)
    {
        if (relay_state[i])
            mask |= (1 << i);
    }

    fprintf(f, "mask,%u\n", mask);

    fprintf(f, "ch");
    for (int i = 0; i < PCA9557_NUM_CHANNELS; i++)
    {
        fprintf(f, ",%u", relay_state[i]);
    }
    fprintf(f, "\n");

    fclose(f);
    ESP_LOGI("PCA9557", "Relay state saved to %s (mask=0x%02X)", path, mask);
}
void relay_load_from_spiffs(void)
{
    const char *path = "/spiffs/relay_pca9557.csv";
    FILE *f = fopen(path, "r");
    if (!f)
    {
        ESP_LOGW("PCA9557", "No relay state file found, skip restore");
        return;
    }

    char line[128];
    uint8_t mask = 0;
    bool found = false;

    while (fgets(line, sizeof(line), f))
    {
        if (strncmp(line, "mask", 4) == 0)
        {
            int val = 0;
            if (sscanf(line, "mask,%d", &val) == 1)
            {
                mask = (uint8_t)val;
                found = true;
            }
        }
    }
    fclose(f);
    if (found)
    {
        relay_set_mask(mask);
        ESP_LOGI("PCA9557", "Restored relay state from %s (mask=0x%02X)", path, mask);
    }
    else
    {
        ESP_LOGW("PCA9557", "No valid relay state found in %s", path);
    }
}

void pca9557_task()
{
    // ESP_ERROR_CHECK(relay_write_mask((1 << 0) | (1 << 3)));
    update_relay_status();
    for (int i = 1; i <= 8; i++)
    {
        bool on;
        if (relay_get(i, &on) == ESP_OK)
            printf("Relay %d = %s\n", i, on ? "ON" : "OFF");
    }
    pca9557_spiffs();
    // uint8_t raw=0;
    // pca9557_read_reg(PCA9557_REG_OUTPUT, &raw);
    // uint8_t logical = raw_to_logical(raw);
    // ESP_LOGI("PCA9557", "RAW=0x%02X  LOGICAL=0x%02X", raw, logical);
    // vTaskDelay(pdMS_TO_TICKS(60000));
}