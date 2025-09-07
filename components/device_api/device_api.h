#ifndef DEVICE_API_H
#define DEVICE_API_H

extern char *mqttarr[2];
extern char uuid[64], key[32], server[64], user[32], pass[32];
extern double sal;
extern int ry1[3], ry2[3], ry3[3], ry4[3], pca[7];
extern char device_time[32], device_exp[16];
extern bool en;
extern bool check_server_alive();
extern bool check_time_device;
extern bool server_connected;
extern bool check_reg_device ;
void load_uuid_from_spiffs(void);

void post_uuid();
void post_time_set();
void get_device_reg();
void get_device_data();
void http_task();
#endif // DEVICE_API_H
