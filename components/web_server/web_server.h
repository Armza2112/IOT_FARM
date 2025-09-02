#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t home(httpd_req_t *req);
esp_err_t wifi_manage(httpd_req_t *req);
esp_err_t disconnect_handle(httpd_req_t *req);
esp_err_t connect_post_handler(httpd_req_t *req);
esp_err_t restart_handle(httpd_req_t *req);
esp_err_t log_file_handler(httpd_req_t *req);

extern httpd_handle_t server_handle ;

void disconnect_wifi_task(void *pvParameter);
void stop_webserver(void);
void stop_server_task(void *pvParameter);
httpd_handle_t start_webserver(void);


#endif