#ifndef RYR404A_MANAGE_H
#define RYR404A_MANAGE_H
#define RYR_CHANNELS_PER_BOARD 4
#define RYR_NUM_BOARDS 4         

extern uint8_t relay[RYR_NUM_BOARDS][RYR_CHANNELS_PER_BOARD];
extern esp_err_t modbus_master_init(void);
extern esp_err_t ryr_write_mask_board(uint8_t slave, uint8_t mask4bits);

void ryr404a_task();
void ryr404a_to_spiffs();
// void load_and_apply_relays_from_spiffs(void);
#endif