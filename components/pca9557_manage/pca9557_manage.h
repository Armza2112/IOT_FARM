#ifndef PCA9557_MANAGE_H
#define PCA9557_MANAGE_H
#define PCA9557_REG_OUTPUT 0x01

extern esp_err_t pca9557_read_reg(uint8_t reg, uint8_t *val);
extern uint8_t raw_to_logical(uint8_t raw);
extern uint8_t relay_state[];
extern esp_err_t pca9557_init_once(void);

void pca9557_task();
void relay_set_mask(uint8_t mask);
void pca9557_spiffs(void);

#endif