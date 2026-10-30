// Compile this with generated, byte-for-byte production method excerpts.
// RTOS scheduling, vendor codecs and hardware are explicitly model boundaries.
#include "boundary.h"
#define private public
#include "production_audio_service.h"
#undef private

namespace {
constexpr EventBits_t STOPPED = (1 << 5) | (1 << 6) | (1 << 7);
constexpr size_t ENCODER_BYTES = 24524, DECODER_BYTES = 17784;
constexpr size_t TCB_BYTES = 256, RESAMPLER_BYTES = 2048;
struct Block {
  uint32_t bytes, used, requested, magic;
};
static_assert(sizeof(Block) == 16);
struct Allocation {
  std::string kind;
  size_t bytes, free_before, largest_before;
  bool success;
};
class Heap {
  alignas(std::max_align_t) std::array<uint8_t, 256 * 1024> arena{};
  size_t end = 0;
  Block *block(size_t at) {
    return reinterpret_cast<Block *>(arena.data() + at);
  }
  size_t next(size_t at) { return at + sizeof(Block) + block(at)->bytes; }

public:
  size_t attempts = 0, fail_at = 0, live = 0, peak = 0;
  std::vector<Allocation> trace;
  void reset(bool fragmented = false) {
    end = attempts = fail_at = live = peak = 0;
    trace.clear();
    std::vector<size_t> holes =
        fragmented ? std::vector<size_t>{32768, 24576, 18432, 12288, 8192, 8192}
                   : std::vector<size_t>{220 * 1024};
    for (size_t i = 0; i < holes.size(); ++i) {
      *block(end) = {uint32_t(holes[i]), 0, 0, 0x41554449};
      end += sizeof(Block) + holes[i];
      if (i + 1 < holes.size()) {
        *block(end) = {16, 2, 0,
                       0x41554449}; // surviving allocation separates holes
        end += sizeof(Block) + 16;
      }
    }
  }
  size_t free_bytes() {
    size_t total = 0;
    for (size_t at = 0; at < end; at = next(at))
      if (!block(at)->used)
        total += block(at)->bytes;
    return total;
  }
  size_t largest() {
    size_t total = 0;
    for (size_t at = 0; at < end; at = next(at))
      if (!block(at)->used)
        total = std::max(total, size_t(block(at)->bytes));
    return total;
  }
  void *allocate(size_t bytes, const char *kind) {
    Allocation record{kind, bytes, free_bytes(), largest(), false};
    ++attempts;
    void *result = nullptr;
    size_t wanted = (bytes + 15) & ~size_t(15);
    if (attempts != fail_at) {
      for (size_t at = 0; at < end; at = next(at)) {
        auto *item = block(at);
        assert(item->magic == 0x41554449);
        if (item->used || item->bytes < wanted)
          continue;
        if (item->bytes >= wanted + sizeof(Block) + 16) {
          *block(at + sizeof(Block) + wanted) = {
              uint32_t(item->bytes - wanted - sizeof(Block)), 0, 0, 0x41554449};
          item->bytes = uint32_t(wanted);
        }
        item->used = 1;
        item->requested = uint32_t(bytes);
        live += item->bytes + sizeof(Block);
        peak = std::max(peak, live);
        result = arena.data() + at + sizeof(Block);
        record.success = true;
        break;
      }
    }
    trace.push_back(record);
    return result;
  }
  void release(void *pointer) {
    if (!pointer)
      return;
    bool found = false;
    for (size_t at = 0; at < end; at = next(at)) {
      if (arena.data() + at + sizeof(Block) != pointer)
        continue;
      auto *item = block(at);
      assert(item->used == 1 && item->magic == 0x41554449);
      live -= item->bytes + sizeof(Block);
      item->used = item->requested = 0;
      found = true;
      break;
    }
    assert(found && "unknown/double free");
    for (size_t at = 0; at < end;) {
      size_t following = next(at);
      if (!block(at)->used && following < end && !block(following)->used)
        block(at)->bytes += uint32_t(sizeof(Block) + block(following)->bytes);
      else
        at = following;
    }
  }
  void reset_trace() {
    attempts = fail_at = 0;
    trace.clear();
  }
} heap;
struct BlockedAtGate {};
struct SelfDeleted {};
bool timer_running = false, timer_fail = false;
unsigned timer_invalid_state_starts = 0, timer_stops = 0;
int frame_query_failure = 0, encoder_error_handle = 0, decoder_error_handle = 0;
unsigned task_bodies = 0, gate_visits = 0, notifications = 0, enc_opens = 0,
         dec_opens = 0;
std::vector<HostTask *> tasks;
thread_local HostTask *current_task = nullptr;
AudioService *active_service = nullptr;
std::mutex encoder_gate_mutex;
std::condition_variable encoder_gate_cv;
bool block_encoder = false, encoder_entered = false, encoder_released = false;
} // namespace

struct HostTask {
  void (*entry)(void *);
  void *argument;
  void *stack;
  const char *name;
  unsigned stack_bytes, notify_count;
  int priority;
  bool gated;
};
static_assert(sizeof(HostTask) <= TCB_BYTES);
AudioCodec host_codec;

EventGroupHandle_t xEventGroupCreate() { return new HostEvents; }
void vEventGroupDelete(EventGroupHandle_t group) { delete group; }
EventBits_t xEventGroupSetBits(EventGroupHandle_t group, EventBits_t bits) {
  return group->bits |= bits;
}
EventBits_t xEventGroupClearBits(EventGroupHandle_t group, EventBits_t bits) {
  auto old = group->bits;
  group->bits &= ~bits;
  return old;
}
EventBits_t xEventGroupGetBits(EventGroupHandle_t group) { return group->bits; }
EventBits_t xEventGroupWaitBits(EventGroupHandle_t group, EventBits_t,
                                int clear, int, TickType_t) {
  assert(!clear);
  return group->bits;
}
BaseType_t xTaskCreate(void (*entry)(void *), const char *name,
                       unsigned stack_bytes, void *argument, int priority,
                       TaskHandle_t *output) {
  *output = nullptr;
  void *stack = heap.allocate(stack_bytes, name);
  if (!stack)
    return pdFAIL;
  void *tcb = heap.allocate(TCB_BYTES, "task_control_block");
  if (!tcb) {
    heap.release(stack);
    return pdFAIL;
  }
  auto *task = new (tcb)
      HostTask{entry, argument, stack, name, stack_bytes, 0, priority, false};
  *output = task;
  tasks.push_back(task);
  current_task = task;
  try {
    entry(argument);
    assert(false && "task was not gated before publication");
  } catch (const BlockedAtGate &) {
    task->gated = true;
  }
  current_task = nullptr;
  return pdPASS;
}
BaseType_t xTaskCreatePinnedToCore(void (*entry)(void *), const char *name,
                                   unsigned stack, void *arg, int priority,
                                   TaskHandle_t *output, int core) {
  assert(core == 0);
  return xTaskCreate(entry, name, stack, arg, priority, output);
}
void vTaskDelete(TaskHandle_t handle) {
  bool self = handle == nullptr;
  auto *task = self ? current_task : handle;
  auto at = std::find(tasks.begin(), tasks.end(), task);
  assert(at != tasks.end() && "deleted an old/self-deleted task handle");
  if (!self)
    assert(task->gated && task->notify_count == 0);
  void *stack = task->stack;
  tasks.erase(at);
  task->~HostTask();
  heap.release(stack);
  heap.release(task);
  if (self)
    throw SelfDeleted{};
}
unsigned ulTaskNotifyTake(int clear, unsigned timeout) {
  assert(current_task && clear == pdTRUE && timeout == portMAX_DELAY);
  ++gate_visits;
  if (!current_task->notify_count)
    throw BlockedAtGate{};
  auto result = current_task->notify_count;
  current_task->notify_count = 0;
  return result;
}
void xTaskNotifyGive(TaskHandle_t task) {
  assert(active_service && task && task->gated);
  assert(!active_service->service_stopped_ && active_service->opus_encoder_ &&
         active_service->opus_decoder_);
  assert(active_service->audio_input_task_handle_ &&
         active_service->audio_output_task_handle_ &&
         active_service->opus_codec_task_handle_);
  assert((active_service->event_group_->bits & STOPPED) == 0 && timer_running &&
         task_bodies == 0);
  ++notifications;
  ++task->notify_count;
}
int esp_timer_create(const esp_timer_create_args_t *,
                     esp_timer_handle_t *output) {
  *output = &timer_running;
  return ESP_OK;
}
int esp_timer_start_periodic(esp_timer_handle_t, uint64_t period) {
  assert(period == 1000000);
  if (timer_fail)
    return -1;
  if (timer_running) {
    ++timer_invalid_state_starts;
    return ESP_ERR_INVALID_STATE;
  }
  timer_running = true;
  return ESP_OK;
}
int esp_timer_stop(esp_timer_handle_t) {
  ++timer_stops;
  if (!timer_running)
    return ESP_ERR_INVALID_STATE;
  timer_running = false;
  return ESP_OK;
}
int64_t esp_timer_get_time() { return 10000000; }
esp_audio_err_t esp_opus_enc_open(const esp_opus_enc_config_t *cfg,
                                  unsigned bytes, void **output) {
  assert(bytes == sizeof(*cfg) && cfg->sample_rate == 16000 &&
         cfg->channel == 1 && cfg->enable_dtx && cfg->enable_vbr);
  assert(cfg->frame_duration == 60 && cfg->complexity == 0 && !cfg->enable_fec);
  ++enc_opens;
  {
    std::unique_lock<std::mutex> lock(encoder_gate_mutex);
    if (block_encoder) {
      encoder_entered = true;
      encoder_gate_cv.notify_all();
      encoder_gate_cv.wait(lock, [] { return encoder_released; });
    }
  }
  *output = heap.allocate(ENCODER_BYTES, "opus_encoder");
  return *output && !encoder_error_handle ? ESP_AUDIO_ERR_OK
                                          : ESP_AUDIO_ERR_MEM_LACK;
}
esp_audio_err_t esp_opus_enc_get_frame_size(void *, int *input, int *output) {
  *input = frame_query_failure == 2   ? 1919
           : frame_query_failure == 3 ? 0
                                      : 1920;
  *output = frame_query_failure == 4 ? 0 : 1276;
  return frame_query_failure == 1 ? -1 : ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_enc_close(void *pointer) {
  heap.release(pointer);
  return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_opus_dec_open(const esp_opus_dec_cfg_t *cfg, unsigned bytes,
                                  void **output) {
  assert(bytes == sizeof(*cfg) && cfg->channel == 1 && !cfg->self_delimited);
  ++dec_opens;
  *output = heap.allocate(DECODER_BYTES, "opus_decoder");
  return *output && !decoder_error_handle ? ESP_AUDIO_ERR_OK
                                          : ESP_AUDIO_ERR_MEM_LACK;
}
esp_audio_err_t esp_opus_dec_close(void *pointer) {
  heap.release(pointer);
  return ESP_AUDIO_ERR_OK;
}
int esp_ae_rate_cvt_open(const esp_ae_rate_cvt_cfg_t *, void **output) {
  *output = heap.allocate(RESAMPLER_BYTES, "rate_converter");
  return *output ? ESP_OK : -1;
}
int esp_ae_rate_cvt_close(void *pointer) {
  heap.release(pointer);
  return ESP_OK;
}

#include "production_audio_methods.inc"

// Hardware task bodies/timer are outside this test. Their production wrappers
// above run to the real notification gate and later acknowledge a requested
// stop.
void AudioService::AudioInputTask() {
  assert(service_stopped_);
  ++task_bodies;
}
void AudioService::AudioOutputTask() {
  assert(service_stopped_);
  ++task_bodies;
}
void AudioService::OpusCodecTask() {
  assert(service_stopped_);
  ++task_bodies;
}
void AudioService::CheckAndUpdateAudioPowerState() {}

namespace {
void reset(bool fragmented = false) {
  assert(tasks.empty());
  heap.reset(fragmented);
  host_codec = AudioCodec{};
  timer_running = timer_fail = false;
  timer_invalid_state_starts = timer_stops = 0;
  frame_query_failure = encoder_error_handle = decoder_error_handle = 0;
  task_bodies = gate_visits = notifications = enc_opens = dec_opens = 0;
  block_encoder = encoder_entered = encoder_released = false;
  active_service = nullptr;
}
void prepare(AudioService &service) {
  service.Initialize(&host_codec);
  active_service = &service;
  assert(!service.opus_encoder_ && !service.opus_decoder_ && enc_opens == 0 &&
         dec_opens == 0);
}
void finish_tasks(AudioService &service) {
  assert(service.service_stopped_);
  auto copy = tasks;
  for (auto *task : copy) {
    current_task = task;
    try {
      task->entry(task->argument);
      assert(false);
    } catch (const SelfDeleted &) {
    }
  }
  current_task = nullptr;
  assert(tasks.empty() && service.WaitStopped(0));
}
void stopped_clean(AudioService &service, size_t expected_live = 0) {
  assert(service.service_stopped_ && service.WaitStopped(0) && tasks.empty() &&
         !timer_running);
  assert(heap.live == expected_live);
}
void complete(AudioService &service) {
  service.Stop();
  finish_tasks(service);
  assert(service.ReleaseStoppedCodecs());
  stopped_clean(service);
}
bool start(AudioService &service) {
  task_bodies = 0;
  active_service = &service;
  return service.Start();
}
void assert_metadata_zero(AudioService &service) {
  assert(!service.opus_encoder_ && !service.opus_decoder_ &&
         !service.output_resampler_);
  assert(service.encoder_sample_rate_ == 0 &&
         service.encoder_duration_ms_ == 0);
  assert(service.encoder_frame_size_ == 0 && service.encoder_outbuf_size_ == 0);
  assert(service.decoder_sample_rate_ == 0 &&
         service.decoder_duration_ms_ == 0 && service.decoder_frame_size_ == 0);
}
void test_release_and_restart() {
  reset();
  AudioService service;
  prepare(service);
  assert(!service.ReleaseStoppedCodecs()); // no tasks have yet acknowledged
  assert(start(service) && tasks.size() == 3 && gate_visits == 3 &&
         notifications == 3);
  assert(service.encoder_frame_size_ == 960 &&
         service.encoder_outbuf_size_ == 1276);
  assert(service.decoder_sample_rate_ == 16000 &&
         service.decoder_frame_size_ == 960);
  auto before = heap.live, attempts = heap.attempts;
  assert(start(service) &&
         heap.attempts == attempts); // idempotent running Start
  assert(!service.ReleaseStoppedCodecs() && heap.live == before);
  service.Stop();
  assert(!service.WaitStopped(0) && !service.ReleaseStoppedCodecs());
  assert(!start(service) &&
         heap.live == before); // stopped flag is not a task acknowledgement
  finish_tasks(service);
  void *encoder = service.opus_encoder_;
  before = heap.live;
  assert(start(service) && service.opus_encoder_ == encoder);
  assert(heap.live > before); // tasks restored, existing codecs retained
  complete(service);
  assert_metadata_zero(service);
  assert(service.ReleaseStoppedCodecs() && heap.live == 0);
  assert(start(service));
  complete(service);
  std::puts("PASS release_stop_ack_restart");
}
void test_stopped_enqueue_and_callback_reentry() {
  reset();
  AudioService service;
  prepare(service);
  service.PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue,
                                std::vector<int16_t>(960, 1));
  assert(service.audio_encode_queue_.empty());
  assert(start(service));
  service.PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue,
                                std::vector<int16_t>(960, 2));
  assert(service.audio_encode_queue_.size() == 1);
  unsigned callback_count = 0;
  service.playback_drained_notified_ = false;
  service.callbacks_.on_playback_drained = [&] {
    ++callback_count;
    // Stop must release its lifecycle lock before invoking external code.
    assert(!service.Start());
    assert(!service.ReleaseStoppedCodecs());
    service.Stop();
  };
  auto stopping = std::async(std::launch::async, [&] { service.Stop(); });
  assert(stopping.wait_for(std::chrono::seconds(2)) ==
         std::future_status::ready);
  stopping.get();
  assert(callback_count == 1 && service.audio_encode_queue_.empty());
  service.PushTaskToEncodeQueue(kAudioTaskTypeEncodeToSendQueue,
                                std::vector<int16_t>(960, 3));
  assert(service.audio_encode_queue_.empty());
  finish_tasks(service);
  assert(service.ReleaseStoppedCodecs());
  stopped_clean(service);
  std::puts("PASS stopped_enqueue_and_Stop_callback_reentry");
}
void test_timer_rearmed_before_stop_ack() {
  reset();
  AudioService service;
  prepare(service);
  assert(esp_timer_stop(service.audio_power_timer_) == ESP_ERR_INVALID_STATE);
  assert(esp_timer_start_periodic(service.audio_power_timer_, 1000000) ==
         ESP_OK);
  assert(esp_timer_start_periodic(service.audio_power_timer_, 1000000) ==
         ESP_ERR_INVALID_STATE);
  assert(esp_timer_stop(service.audio_power_timer_) == ESP_OK);
  assert(esp_timer_stop(service.audio_power_timer_) == ESP_ERR_INVALID_STATE);
  assert(start(service));
  for (unsigned cycle = 0; cycle < 10; ++cycle) {
    service.Stop();
    assert(!timer_running && !service.WaitStopped(0));
    // Model the existing ReadAudioData/AudioOutputTask timer API boundary:
    // an in-flight old task can rearm after Stop, before acknowledging exit.
    assert(esp_timer_start_periodic(service.audio_power_timer_, 1000000) ==
           ESP_OK);
    assert(timer_running);
    auto allocations = heap.attempts;
    assert(!start(service));
    assert(heap.attempts == allocations && timer_running);
    finish_tasks(service);
    assert(timer_running && service.WaitStopped(0));
    auto invalid_starts = timer_invalid_state_starts, stops = timer_stops;
    assert(start(service) && "restart must stop a timer rearmed by old tasks");
    assert(timer_running && timer_stops > stops &&
           timer_invalid_state_starts == invalid_starts);
  }
  complete(service);
  std::puts("PASS timer_active_INVALID_STATE_and_10_pre_ack_rearm_restarts");
}
size_t test_failure_injection() {
  reset();
  size_t allocations;
  {
    AudioService service;
    prepare(service);
    assert(start(service));
    allocations = heap.attempts;
    assert(allocations == 8);
    complete(service);
  }
  for (size_t failure = 1; failure <= allocations; ++failure) {
    reset();
    AudioService service;
    prepare(service);
    heap.fail_at = failure;
    assert(!start(service));
    stopped_clean(service);
    assert(!service.opus_encoder_ && !service.opus_decoder_ &&
           notifications == 0 && task_bodies == 0);
    assert(!service.audio_input_task_handle_ &&
           !service.audio_output_task_handle_ &&
           !service.opus_codec_task_handle_);
    heap.reset_trace();
    assert(start(service));
    complete(service);
  }
  for (int phase = 1; phase <= 7; ++phase) {
    reset();
    AudioService service;
    prepare(service);
    frame_query_failure = phase <= 4 ? phase : 0;
    encoder_error_handle = phase == 5;
    decoder_error_handle = phase == 6;
    timer_fail = phase == 7;
    assert(!start(service));
    stopped_clean(service);
    assert(notifications == 0 && task_bodies == 0);
    frame_query_failure = encoder_error_handle = decoder_error_handle = 0;
    timer_fail = false;
    assert(start(service));
    complete(service);
  }
  // When stopped codecs already existed, a task/timer failure must not destroy
  // them.
  for (size_t failure = 1; failure <= 7; ++failure) {
    reset();
    AudioService service;
    prepare(service);
    assert(start(service));
    service.Stop();
    finish_tasks(service);
    auto before = heap.live;
    auto enc = service.opus_encoder_;
    auto dec = service.opus_decoder_;
    heap.reset_trace();
    if (failure <= 6)
      heap.fail_at = failure;
    else
      timer_fail = true;
    assert(!start(service));
    stopped_clean(service, before);
    assert(service.opus_encoder_ == enc && service.opus_decoder_ == dec);
    heap.reset_trace();
    timer_fail = false;
    assert(start(service));
    complete(service);
  }
  std::puts("PASS allocation_and_vendor_error_rollback_retry");
  return allocations;
}
void test_resamplers_and_same_rate_null() {
  reset();
  {
    AudioService service;
    prepare(service);
    service.decoder_sample_rate_ = 16000;
    service.decoder_duration_ms_ = 60;
    assert(!service.opus_decoder_);
    service.SetDecodeSampleRate(16000, 60);
    assert(service.opus_decoder_ && dec_opens == 1 &&
           service.decoder_frame_size_ == 960);
    auto attempts = heap.attempts;
    service.SetDecodeSampleRate(16000, 60);
    assert(heap.attempts == attempts);
    service.SetDecodeSampleRate(24000, 40);
    assert(service.opus_decoder_ && service.output_resampler_ &&
           service.decoder_frame_size_ == 960);
    service.event_group_->bits |= STOPPED;
    assert(service.ReleaseStoppedCodecs());
    stopped_clean(service);
  }
  for (size_t failed = 1; failed <= 2; ++failed) {
    reset();
    AudioService service;
    prepare(service);
    heap.fail_at = failed;
    service.SetDecodeSampleRate(24000, 60);
    if (failed == 1)
      assert(!service.opus_decoder_ && service.decoder_frame_size_ == 0);
    else
      assert(service.opus_decoder_ && !service.output_resampler_);
    service.event_group_->bits |= STOPPED;
    assert(service.ReleaseStoppedCodecs());
    stopped_clean(service);
    heap.reset_trace();
    service.SetDecodeSampleRate(24000, 60);
    assert(service.opus_decoder_ && service.output_resampler_);
    assert(service.ReleaseStoppedCodecs());
    stopped_clean(service);
  }
  reset();
  {
    AudioService service;
    host_codec.input_rate = 24000;
    prepare(service);
    auto *retained = service.input_resampler_;
    assert(retained);
    auto baseline = heap.live;
    assert(start(service));
    service.SetDecodeSampleRate(24000, 60);
    assert(service.output_resampler_);
    service.audio_send_queue_.push_back(std::make_unique<AudioStreamPacket>());
    service.Stop();
    finish_tasks(service);
    assert(service.ReleaseStoppedCodecs());
    stopped_clean(service, baseline);
    assert(service.input_resampler_ == retained &&
           service.audio_send_queue_.empty());
    assert_metadata_zero(service);
    assert(service.ReleaseStoppedCodecs() && heap.live == baseline);
  }
  assert(heap.live == 0);
  std::puts("PASS decoder_null_same_rate_resampler_and_config_reset");
}
[[maybe_unused]] size_t test_fragmented_and_cycles() {
  // This is an explanatory synthetic free-block profile, not a device dump.
  reset(true);
  auto total = heap.free_bytes(), largest = heap.largest();
  void *decoder = heap.allocate(DECODER_BYTES, "counterfactual_decoder_first");
  void *encoder = heap.allocate(ENCODER_BYTES, "counterfactual_encoder_second");
  assert(decoder && encoder && heap.free_bytes() > 24576 &&
         heap.largest() < 24576);
  assert(!heap.allocate(24576, "counterfactual_late_task_stack"));
  heap.release(encoder);
  heap.release(decoder);
  assert(heap.free_bytes() == total && heap.largest() == largest &&
         heap.live == 0);
  reset(true);
  AudioService service;
  prepare(service);
  size_t peak = 0;
  for (unsigned cycle = 0; cycle < 50; ++cycle) {
    heap.reset_trace();
    assert(start(service));
#if CONFIG_BOARD_TYPE_FOLOTOY_AI_PASSPORT_NABO
    assert(heap.trace[0].kind == "opus_codec" && heap.trace[0].bytes == 24576);
    assert(heap.trace[2].kind == "opus_encoder" &&
           heap.trace[3].kind == "opus_decoder");
#endif
    peak = std::max(peak, heap.live);
    complete(service);
    assert(heap.free_bytes() == total && heap.largest() == largest);
  }
  std::puts("PASS fragmented_first_fit_and_50_cycles");
  return peak;
}
void test_lifecycle_serialization(bool call_stop) {
  reset();
  AudioService service;
  prepare(service);
  block_encoder = true;
  auto starting =
      std::async(std::launch::async, [&] { return start(service); });
  {
    std::unique_lock<std::mutex> lock(encoder_gate_mutex);
    assert(encoder_gate_cv.wait_for(lock, std::chrono::seconds(2),
                                    [] { return encoder_entered; }));
  }
  auto other = std::async(std::launch::async, [&] {
    if (call_stop) {
      service.Stop();
      return true;
    }
    return service.ReleaseStoppedCodecs();
  });
  assert(other.wait_for(std::chrono::milliseconds(20)) ==
         std::future_status::timeout);
  {
    std::lock_guard<std::mutex> lock(encoder_gate_mutex);
    encoder_released = true;
  }
  encoder_gate_cv.notify_all();
  assert(starting.get());
  assert(other.get() == call_stop);
  if (!call_stop)
    service.Stop();
  finish_tasks(service);
  assert(service.ReleaseStoppedCodecs());
  stopped_clean(service);
  std::puts(call_stop ? "PASS Start_Stop_lifecycle_mutex"
                      : "PASS Start_Release_lifecycle_mutex");
}
} // namespace

int main() {
  test_release_and_restart();
  test_stopped_enqueue_and_callback_reentry();
  test_timer_rearmed_before_stop_ack();
  auto failure_sites = test_failure_injection();
  test_resamplers_and_same_rate_null();
#if CONFIG_BOARD_TYPE_FOLOTOY_AI_PASSPORT_NABO
  auto peak = test_fragmented_and_cycles();
#else
  size_t peak = 0; // other boards keep their existing task ordering, exercised
                   // on healthy heap
#endif
  test_lifecycle_serialization(false);
  test_lifecycle_serialization(true);
  std::printf("AUDIO_HOST_JSON "
              "{\"status\":\"PASS\",\"nabo\":%d,\"processor\":%d,\"allocation_"
              "failure_sites\":%zu,\"vendor_timer_error_cases\":7,\"retained_"
              "codec_failure_cases\":7,\"cycles\":%d,\"peak_modeled_bytes\":%"
              "zu,\"encoder_bytes\":%zu,\"decoder_bytes\":%zu,\"opus_task_"
              "stack_bytes\":24576,\"tcb_model_bytes\":%zu}\n",
              CONFIG_BOARD_TYPE_FOLOTOY_AI_PASSPORT_NABO,
              CONFIG_USE_AUDIO_PROCESSOR, failure_sites,
              CONFIG_BOARD_TYPE_FOLOTOY_AI_PASSPORT_NABO ? 50 : 0, peak,
              ENCODER_BYTES, DECODER_BYTES, TCB_BYTES);
  return 0;
}
