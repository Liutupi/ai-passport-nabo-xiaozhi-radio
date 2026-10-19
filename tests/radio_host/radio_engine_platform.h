#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern int64_t host_engine_us;
static inline int64_t esp_timer_get_time(void) { return host_engine_us; }
#define ESP_LOGI(tag, ...) ((void)(tag))
#define ESP_OK 0
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define pdMS_TO_TICKS(n) (n)
#define pdPASS 1
typedef void *TaskHandle_t;
static inline int xTaskCreate(void (*fn)(void *), const char *name,
                              unsigned stack, void *arg, int priority,
                              TaskHandle_t *task) {
  (void)fn;
  (void)name;
  (void)stack;
  (void)arg;
  (void)priority;
  *task = (void *)1;
  return pdPASS;
}
static inline void vTaskDelete(TaskHandle_t task) { (void)task; }
typedef struct {
  size_t size, used;
  uint8_t data[];
} *StreamBufferHandle_t;
static inline StreamBufferHandle_t xStreamBufferCreate(size_t n,
                                                       size_t trigger) {
  (void)trigger;
  StreamBufferHandle_t s = calloc(1, sizeof(*s) + n);
  if (s)
    s->size = n;
  return s;
}
static inline void vStreamBufferDelete(StreamBufferHandle_t s) { free(s); }
static inline size_t xStreamBufferBytesAvailable(StreamBufferHandle_t s) {
  return s ? s->used : 0;
}
static inline size_t xStreamBufferSend(StreamBufferHandle_t s, const void *data,
                                       size_t n, unsigned wait) {
  (void)wait;
  if (n > s->size - s->used)
    n = s->size - s->used;
  memcpy(s->data + s->used, data, n);
  s->used += n;
  return n;
}
static inline size_t xStreamBufferReceive(StreamBufferHandle_t s, void *data,
                                          size_t n, unsigned wait) {
  (void)wait;
  if (n > s->used)
    n = s->used;
  memcpy(data, s->data, n);
  s->used -= n;
  memmove(s->data, s->data + n, s->used);
  return n;
}
typedef int nvs_handle_t;
typedef int esp_err_t;
#define NVS_READWRITE 1
#define NVS_READONLY 0
static inline int nvs_open(const char *name, int mode, nvs_handle_t *h) {
  (void)name;
  (void)mode;
  *h = 0;
  return -1;
}
static inline int host_nvs_get(nvs_handle_t h, const char *key, void *out) {
  (void)h;
  (void)key;
  (void)out;
  return -1;
}
static inline int host_nvs_set(nvs_handle_t h, const char *key,
                               uint64_t value) {
  (void)h;
  (void)key;
  (void)value;
  return 0;
}
static inline int nvs_get_blob(nvs_handle_t h, const char *key, void *out,
                               size_t *n) {
  (void)n;
  return host_nvs_get(h, key, out);
}
static inline int nvs_set_blob(nvs_handle_t h, const char *key, const void *in,
                               size_t n) {
  (void)in;
  return host_nvs_set(h, key, n);
}
#define nvs_get_u8 host_nvs_get
#define nvs_get_u32 host_nvs_get
#define nvs_get_u64 host_nvs_get
#define nvs_get_i32 host_nvs_get
#define nvs_set_u8 host_nvs_set
#define nvs_set_u32 host_nvs_set
#define nvs_set_i32 host_nvs_set
static inline int nvs_commit(nvs_handle_t h) {
  (void)h;
  return 0;
}
static inline void nvs_close(nvs_handle_t h) { (void)h; }
typedef int esp_audio_err_t;
#define ESP_AUDIO_ERR_OK 0
typedef struct {
  int unused;
} esp_aac_dec_cfg_t;
#define ESP_AAC_DEC_CONFIG_DEFAULT() {0}
typedef struct {
  uint8_t *buffer;
  uint32_t len, consumed;
} esp_audio_dec_in_raw_t;
typedef struct {
  uint8_t *buffer;
  uint32_t len, needed_size, decoded_size;
} esp_audio_dec_out_frame_t;
typedef struct {
  uint32_t sample_rate;
  uint8_t bits_per_sample, channel;
} esp_audio_dec_info_t;
static inline int esp_aac_dec_open(void *cfg, unsigned size, void **out) {
  (void)cfg;
  (void)size;
  (void)out;
  return -1;
}
static inline int esp_aac_dec_close(void *p) {
  (void)p;
  return 0;
}
static inline int esp_aac_dec_decode(void *p, esp_audio_dec_in_raw_t *raw,
                                     esp_audio_dec_out_frame_t *out,
                                     esp_audio_dec_info_t *info) {
  (void)p;
  (void)raw;
  (void)out;
  (void)info;
  return -1;
}
