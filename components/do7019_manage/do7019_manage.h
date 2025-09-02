#ifndef DO7019_MANAGE_H
#define DO7019_MANAGE_H
#include <time.h>   
#define HISTORY_SIZE 100
#define TIME_STR_LEN 6
extern float temp_history[HISTORY_SIZE];
extern float oxygen_history[HISTORY_SIZE];
extern float salinity_history[HISTORY_SIZE];
extern float do_history[HISTORY_SIZE];
extern char time_history[HISTORY_SIZE][TIME_STR_LEN];
extern int history_index ;
void do7019_task();
#endif // DO7019_MANAGE_H
