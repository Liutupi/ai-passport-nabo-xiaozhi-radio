#pragma once
/* Scripted, memory-only HTTP/RTOS boundary for the real network_worker. */
#include <stdbool.h>
#include <stdint.h>

#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_ERR_HTTP_EAGAIN 0x7007
#define HTTP_EVENT_ON_HEADER 1
#define HTTP_EVENT_ON_DATA 2

typedef struct host_http_client *esp_http_client_handle_t;
typedef struct {
  int event_id;
  esp_http_client_handle_t client;
  void *user_data;
  char *header_key;
  char *header_value;
  void *data;
  int data_len;
} esp_http_client_event_t;
typedef struct {
  const char *url;
  int (*crt_bundle_attach)(void *);
  int timeout_ms, buffer_size, buffer_size_tx;
  int (*event_handler)(esp_http_client_event_t *);
  void *user_data;
  int max_redirection_count;
} esp_http_client_config_t;

int esp_crt_bundle_attach(void *ignored);
esp_http_client_handle_t
esp_http_client_init(const esp_http_client_config_t *config);
int esp_http_client_set_header(esp_http_client_handle_t http, const char *key,
                               const char *value);
int esp_http_client_open(esp_http_client_handle_t http, int length);
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t http);
int esp_http_client_get_status_code(esp_http_client_handle_t http);
int esp_http_client_set_redirection(esp_http_client_handle_t http);
int esp_http_client_close(esp_http_client_handle_t http);
int esp_http_client_set_timeout_ms(esp_http_client_handle_t http, int timeout);
int esp_http_client_get_socket(esp_http_client_handle_t http);
int radio_socket_receive_timeout(int fd, unsigned timeout_ms);
int esp_http_client_read(esp_http_client_handle_t http, char *data,
                         int capacity);
int esp_http_client_get_errno(esp_http_client_handle_t http);
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t http);
int esp_http_client_cleanup(esp_http_client_handle_t http);
void vTaskDelay(unsigned ticks);
void vTaskSuspend(TaskHandle_t task);
