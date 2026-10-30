#pragma once
// Only hardware/vendor boundaries are stubs. The generated class, queue and
// lifecycle methods come from production source without implementation edits.
#include "fixed_queue.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using EventBits_t = uint32_t;
using BaseType_t = int;
using TickType_t = unsigned;
struct HostEvents {
  EventBits_t bits = 0;
};
using EventGroupHandle_t = HostEvents *;
struct HostTask;
using TaskHandle_t = HostTask *;
using esp_timer_handle_t = void *;
using esp_ae_rate_cvt_handle_t = void *;
using esp_err_t = int;
using esp_audio_err_t = int;
using esp_opus_enc_frame_duration_t = int;
using esp_opus_dec_frame_duration_t = int;
struct srmodel_list_t {};
struct AudioStreamPacket {
  std::vector<uint8_t> payload;
};
class AudioDebugger {};
class AudioEngine {
public:
  virtual ~AudioEngine() = default;
  template <class T> void OnOutput(T) {}
  template <class T> void OnVadStateChange(T) {}
  template <class T> void OnWakeWordDetected(T) {}
};
class LiteAudioEngine : public AudioEngine {};
class AfeAudioEngine : public AudioEngine {};
class AudioCodec {
public:
  int input_rate = 16000, output_rate = 16000, channels = 1, starts = 0;
  void Start() { ++starts; }
  int input_sample_rate() const { return input_rate; }
  int output_sample_rate() const { return output_rate; }
  int input_channels() const { return channels; }
};
extern AudioCodec host_codec;
class Board {
public:
  static Board &GetInstance() {
    static Board board;
    return board;
  }
  AudioCodec *GetAudioCodec() { return &host_codec; }
};

constexpr int pdTRUE = 1, pdFALSE = 0, pdPASS = 1, pdFAIL = 0;
constexpr unsigned portMAX_DELAY = ~0u;
constexpr int ESP_OK = 0, ESP_AUDIO_ERR_OK = 0, ESP_AUDIO_ERR_MEM_LACK = -1;
constexpr int ESP_ERR_INVALID_STATE = 0x103;
constexpr int ESP_AUDIO_SAMPLE_RATE_16K = 16000, ESP_AUDIO_MONO = 1,
              ESP_AUDIO_BIT16 = 16;
constexpr int ESP_OPUS_BITRATE_AUTO = 0, ESP_OPUS_ENC_APPLICATION_AUDIO = 1;
constexpr int ESP_AE_RATE_CVT_PERF_TYPE_SPEED = 0, ESP_TIMER_TASK = 0;
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_OPUS_ENC_FRAME_DURATION_5_MS 5
#define ESP_OPUS_ENC_FRAME_DURATION_10_MS 10
#define ESP_OPUS_ENC_FRAME_DURATION_20_MS 20
#define ESP_OPUS_ENC_FRAME_DURATION_40_MS 40
#define ESP_OPUS_ENC_FRAME_DURATION_60_MS 60
#define ESP_OPUS_ENC_FRAME_DURATION_80_MS 80
#define ESP_OPUS_ENC_FRAME_DURATION_100_MS 100
#define ESP_OPUS_ENC_FRAME_DURATION_120_MS 120
struct esp_opus_enc_config_t {
  int sample_rate, channel, bits_per_sample, bitrate, frame_duration;
  int application_mode, complexity;
  bool enable_fec, enable_dtx, enable_vbr;
};
struct esp_opus_dec_cfg_t {
  uint32_t sample_rate;
  int channel, frame_duration;
  bool self_delimited;
};
struct esp_ae_rate_cvt_cfg_t {
  uint32_t src_rate, dest_rate;
  uint8_t channel;
  int bits_per_sample, complexity, perf_type;
};
struct esp_timer_create_args_t {
  void (*callback)(void *);
  void *arg;
  int dispatch_method;
  const char *name;
  bool skip_unhandled_events;
};
EventGroupHandle_t xEventGroupCreate();
void vEventGroupDelete(EventGroupHandle_t);
EventBits_t xEventGroupSetBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupClearBits(EventGroupHandle_t, EventBits_t);
EventBits_t xEventGroupGetBits(EventGroupHandle_t);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t, EventBits_t, int, int,
                                TickType_t);
BaseType_t xTaskCreate(void (*)(void *), const char *, unsigned, void *, int,
                       TaskHandle_t *);
BaseType_t xTaskCreatePinnedToCore(void (*)(void *), const char *, unsigned,
                                   void *, int, TaskHandle_t *, int);
void vTaskDelete(TaskHandle_t);
unsigned ulTaskNotifyTake(int, unsigned);
void xTaskNotifyGive(TaskHandle_t);
int esp_timer_create(const esp_timer_create_args_t *, esp_timer_handle_t *);
int esp_timer_start_periodic(esp_timer_handle_t, uint64_t);
int esp_timer_stop(esp_timer_handle_t);
int64_t esp_timer_get_time();
esp_audio_err_t esp_opus_enc_open(const esp_opus_enc_config_t *, unsigned,
                                  void **);
esp_audio_err_t esp_opus_enc_get_frame_size(void *, int *, int *);
esp_audio_err_t esp_opus_enc_close(void *);
esp_audio_err_t esp_opus_dec_open(const esp_opus_dec_cfg_t *, unsigned,
                                  void **);
esp_audio_err_t esp_opus_dec_close(void *);
int esp_ae_rate_cvt_open(const esp_ae_rate_cvt_cfg_t *, void **);
int esp_ae_rate_cvt_close(void *);
