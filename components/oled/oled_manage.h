#ifndef OLED_MANAGE_H
#define OLED_MANAGE_H

void oled_init();
void initialize_sntp();
void i2c_master_init();
void draw_wifi_screen();
void draw_startup();
void draw_main_screen();

extern bool blink_state;
#endif
