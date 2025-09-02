#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "mbcontroller.h"
#include "esp_modbus_common.h"

#define TAG "RYR404A"

#define MB_PORT_NUM UART_NUM_2
#define MB_DEV_SPEED 9600
#define MB_PARITY_CFG MB_PARITY_NONE

#define MB_UART_TXD 17
#define MB_UART_RXD 16
#define MB_UART_RTS 5

#define RYR_NUM_BOARDS 4
#define RYR_CHANNELS_PER_BOARD 4
#define COIL_ON 0xFF00
#define COIL_OFF 0x0000

#ifndef MB_FUNC_READ_COILS
#define MB_FUNC_READ_COILS 0x01
#endif
#ifndef MB_FUNC_WRITE_MULTIPLE_COILS
#define MB_FUNC_WRITE_MULTIPLE_COILS 0x0F
#endif

static const uint8_t address_boards[RYR_NUM_BOARDS] = {0x01, 0x02, 0x03, 0x04};
uint8_t relay[RYR_NUM_BOARDS][RYR_CHANNELS_PER_BOARD];
void load_and_apply_relays_from_spiffs(void);

esp_err_t modbus_master_init(void)
{
    void *mb_handle = NULL;
    ESP_ERROR_CHECK(mbc_master_init(MB_PORT_SERIAL_MASTER, &mb_handle));

    mb_communication_info_t comm = {
        .port = MB_PORT_NUM,
        .mode = MB_MODE_RTU,
        .baudrate = MB_DEV_SPEED,
        .parity = MB_PARITY_CFG};
    ESP_ERROR_CHECK(mbc_master_setup((void *)&comm));

    ESP_ERROR_CHECK(uart_set_pin(MB_PORT_NUM, MB_UART_TXD, MB_UART_RXD,
                                 MB_UART_RTS, UART_PIN_NO_CHANGE));

    ESP_ERROR_CHECK(mbc_master_start());

    vTaskDelay(pdMS_TO_TICKS(5));
    ESP_ERROR_CHECK(uart_set_mode(MB_PORT_NUM, UART_MODE_RS485_HALF_DUPLEX));

    ESP_LOGI(TAG, "Modbus master started on UART%d, %d 8N1, RTS=%d",
             MB_PORT_NUM, MB_DEV_SPEED, MB_UART_RTS);
    return ESP_OK;
}

esp_err_t ryr_write_mask_board(uint8_t slave, uint8_t mask4bits)
{
    uint8_t payload = (mask4bits & 0x0F);
    mb_param_request_t req = {
        .slave_addr = slave,
        .command = MB_FUNC_WRITE_MULTIPLE_COILS,
        .reg_start = 0,
        .reg_size = RYR_CHANNELS_PER_BOARD,
    };
    return mbc_master_send_request(&req, &payload);
}

esp_err_t ryr_read_mask_board(uint8_t slave, uint8_t *mask4bits_out)
{
    if (!mask4bits_out)
        return ESP_ERR_INVALID_ARG;
    uint8_t payload = 0;
    mb_param_request_t req = {
        .slave_addr = slave,
        .command = MB_FUNC_READ_COILS,
        .reg_start = 0,
        .reg_size = RYR_CHANNELS_PER_BOARD,
    };
    esp_err_t err = mbc_master_send_request(&req, &payload);
    if (err == ESP_OK)
        *mask4bits_out = (payload & 0x0F);
    return err;
}

void read_relay_board(uint8_t slave, uint8_t out[RYR_CHANNELS_PER_BOARD])
{
    uint8_t mask = 0;
    if (ryr_read_mask_board(slave, &mask) == ESP_OK)
    {
        for (int i = 0; i < RYR_CHANNELS_PER_BOARD; ++i)
        {
            out[i] = (mask >> i) & 1;
        }
    }
    else
    {

        memset(out, 0, RYR_CHANNELS_PER_BOARD);
    }
}
void add_relay_status(void)
{
    static const uint8_t SLAVES[RYR_NUM_BOARDS] = {0x01, 0x02, 0x03, 0x04};
    for (int b = 0; b < RYR_NUM_BOARDS; ++b)
    {
        read_relay_board(SLAVES[b], relay[b]);
        ESP_LOGI(TAG, "Relay%d -> ch1:%u, ch2:%u, ch3:%u, ch4:%u",
                 b + 1, relay[b][0], relay[b][1], relay[b][2], relay[b][3]);
    }
}
void ryr404a_to_spiffs()
{
    const char *path = "/spiffs/relay_ryr404a.csv";
    FILE *f = fopen(path, "w");
    if (!f)
    {
        ESP_LOGE(TAG, "open %s failed", path);
        return;
    }
    fprintf(f, "board,ch1,ch2,ch3,ch4\n");
    for (int b = 0; b < RYR_NUM_BOARDS; ++b)
    {
        fprintf(f, "%d,%u,%u,%u,%u\n", b + 1,
                relay[b][0], relay[b][1], relay[b][2], relay[b][3]);
    }
    fclose(f);
    ESP_LOGI(TAG, "Wrote relay states to %s", path);
}
void ryr404a_task()
{
    load_and_apply_relays_from_spiffs();
    while (1)
    {
        add_relay_status();
        ryr404a_to_spiffs();
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
void load_and_apply_relays_from_spiffs(void)
{
    const char *path = "/spiffs/relay_ryr404a.csv";
    FILE *f = fopen(path, "r");
    if (!f)
    {
        ESP_LOGW(TAG, "open(%s) failed", path);
        return; 
    }

    char line[128];
    int applied = 0;

    while (fgets(line, sizeof(line), f))
    {
        int board;
        unsigned c1, c2, c3, c4;

        if (sscanf(line, "%d,%u,%u,%u,%u", &board, &c1, &c2, &c3, &c4) != 5)
        {
            ESP_LOGW(TAG, "skip bad line: %s", line);
            continue;
        }

        if (board < 1 || board > RYR_NUM_BOARDS)
        {
            ESP_LOGW(TAG, "skip out-of-range board=%d", board);
            continue;
        }

        uint8_t ch[4] = {
            (c1 ? 1 : 0),
            (c2 ? 1 : 0),
            (c3 ? 1 : 0),
            (c4 ? 1 : 0),
        };

        uint8_t mask = (ch[0] << 0) |
                       (ch[1] << 1) |
                       (ch[2] << 2) |
                       (ch[3] << 3);

        uint8_t slave = address_boards[board - 1];
        esp_err_t err = ryr_write_mask_board(slave, mask);
        if (err == ESP_OK)
        {
            relay[board - 1][0] = ch[0];
            relay[board - 1][1] = ch[1];
            relay[board - 1][2] = ch[2];
            relay[board - 1][3] = ch[3];
            applied++;
            ESP_LOGI(TAG, "Applied board=%d (slave=0x%02X) mask=0b%04u", board, slave, mask);
        }
        else
        {
            ESP_LOGE(TAG, "Apply board=%d fail err=0x%x", board, err);
        }
    }

    fclose(f);
    ESP_LOGI(TAG, "Applied %d board states from %s", applied, path);
}
