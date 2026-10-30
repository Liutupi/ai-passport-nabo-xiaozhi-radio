/* Real production pipeline on one bounded, coalescing first-fit heap.
 * Heap holes model surviving voice/UI allocations, not a device heap dump.
 * No task, network connection, serial port or physical device is started.
 */
#define NRF_ENGINE_HOST
#include "fragmented_heap.h"
#define radio_heap_free unused_radio_heap_free
#define radio_heap_largest unused_radio_heap_largest
#define ring_block_malloc unused_ring_block_malloc
#define ring_block_free unused_ring_block_free
#define xTaskCreate unused_xTaskCreate
#define vTaskDelete unused_vTaskDelete
#define esp_aac_dec_decode unused_esp_aac_dec_decode
#include "radio_engine_platform.h"
#undef radio_heap_free
#undef radio_heap_largest
#undef ring_block_malloc
#undef ring_block_free
#undef xTaskCreate
#undef vTaskDelete
#undef esp_aac_dec_decode

static int fheap_task_create(void (*function)(void *), const char *name,
                             unsigned stack, void *argument, int priority,
                             TaskHandle_t *task);
static void fheap_task_delete(TaskHandle_t task);
static int fheap_aac_decode(void *decoder, esp_audio_dec_in_raw_t *raw,
                            esp_audio_dec_out_frame_t *out,
                            esp_audio_dec_info_t *info);
#define radio_heap_free fheap_free_bytes
#define radio_heap_largest fheap_largest
#define ring_block_malloc fheap_malloc
#define ring_block_free fheap_free
#define xTaskCreate fheap_task_create
#define vTaskDelete fheap_task_delete
#define esp_aac_dec_decode fheap_aac_decode

/* Platform overrides must precede this included production implementation. */
#include "nabo_radio.c"

#include "coder.h"
#include <assert.h>
#include <stddef.h>

enum { ARENA_BYTES = 256 * 1024, ALIGNMENT = 16, MAX_TRACE = 256 };
enum { FREE_BLOCK = 0, USED_BLOCK = 1, MAGIC = 0x4e41424f };
typedef struct {
  uint32_t bytes, used, requested, magic;
} block_t;
_Static_assert(sizeof(block_t) == 16,
               "allocator metadata must remain explicit");
typedef struct {
  size_t requested, free_before, largest_before, live_after;
  unsigned success, injected;
} allocation_t;
static _Alignas(max_align_t) uint8_t arena[ARENA_BYTES];
static uint32_t arena_size;
static size_t live_reserved, peak_reserved, baseline_reserved, initial_free,
    initial_largest;
static size_t min_free, maximum_request, largest_failed_request;
static unsigned live_blocks, baseline_blocks, bad_frees, attempts, failures,
    injected_failures;
static int fail_index = -1;
static bool tracing;
static allocation_t trace[MAX_TRACE];
static unsigned aac_decoded_bytes, aac_output_capacity;

int64_t host_engine_us = 1000000;
size_t host_ring_limit = 32768;
int host_ring_fail_after = -1, host_ring_allocated;
size_t host_pipeline_heap, host_pipeline_largest;

static block_t *at(uint32_t offset) { return (block_t *)(arena + offset); }
static uint32_t next_offset(uint32_t offset) {
  return offset + sizeof(block_t) + at(offset)->bytes;
}
size_t fheap_free_bytes(void) {
  size_t bytes = 0;
  for (uint32_t offset = 0; offset < arena_size; offset = next_offset(offset)) {
    block_t *block = at(offset);
    assert(block->magic == MAGIC && next_offset(offset) <= arena_size);
    if (!block->used)
      bytes += block->bytes;
  }
  return bytes;
}
size_t fheap_largest(void) {
  size_t bytes = 0;
  for (uint32_t offset = 0; offset < arena_size; offset = next_offset(offset)) {
    block_t *block = at(offset);
    assert(block->magic == MAGIC);
    if (!block->used && block->bytes > bytes)
      bytes = block->bytes;
  }
  return bytes;
}
void *fheap_malloc(size_t bytes) {
  allocation_t record = {.requested = bytes,
                         .free_before = fheap_free_bytes(),
                         .largest_before = fheap_largest()};
  if (tracing) {
    ++attempts;
    if (bytes > maximum_request)
      maximum_request = bytes;
  }
  void *result = NULL;
  bool injected = tracing && fail_index > 0 && attempts == (unsigned)fail_index;
  size_t needed =
      bytes ? (bytes + ALIGNMENT - 1) & ~(size_t)(ALIGNMENT - 1) : ALIGNMENT;
  if (!injected && bytes <= ARENA_BYTES && needed <= ARENA_BYTES) {
    for (uint32_t offset = 0; offset < arena_size;
         offset = next_offset(offset)) {
      block_t *block = at(offset);
      if (block->used || block->bytes < needed)
        continue;
      if (block->bytes - needed >= sizeof(block_t) + ALIGNMENT) {
        uint32_t split = offset + sizeof(block_t) + (uint32_t)needed;
        *at(split) = (block_t){.bytes = block->bytes - (uint32_t)needed -
                                        sizeof(block_t),
                               .magic = MAGIC};
        block->bytes = (uint32_t)needed;
      }
      block->used = USED_BLOCK;
      block->requested = (uint32_t)bytes;
      live_reserved += block->bytes + sizeof(block_t);
      ++live_blocks;
      if (live_reserved > peak_reserved)
        peak_reserved = live_reserved;
      result = (uint8_t *)block + sizeof(block_t);
      break;
    }
  }
  if (tracing) {
    if (!result) {
      ++failures;
      injected_failures += injected;
      if (bytes > largest_failed_request)
        largest_failed_request = bytes;
    }
    size_t remaining = fheap_free_bytes();
    if (remaining < min_free)
      min_free = remaining;
    record.success = result != NULL;
    record.injected = injected;
    record.live_after = live_reserved - baseline_reserved;
    if (attempts <= MAX_TRACE)
      trace[attempts - 1] = record;
  }
  return result;
}
void *fheap_calloc(size_t count, size_t bytes) {
  if (count && bytes > SIZE_MAX / count)
    return NULL;
  size_t total = count * bytes;
  void *result = fheap_malloc(total);
  if (result)
    memset(result, 0, total);
  return result;
}
void fheap_free(void *pointer) {
  if (!pointer)
    return;
  uint32_t previous = UINT32_MAX, found = UINT32_MAX;
  for (uint32_t offset = 0; offset < arena_size; offset = next_offset(offset)) {
    if ((uintptr_t)pointer == (uintptr_t)(arena + offset + sizeof(block_t))) {
      found = offset;
      break;
    }
    previous = offset;
  }
  if (found == UINT32_MAX || at(found)->used != USED_BLOCK) {
    ++bad_frees;
    return;
  }
  block_t *block = at(found);
  live_reserved -= block->bytes + sizeof(block_t);
  --live_blocks;
  block->used = FREE_BLOCK;
  block->requested = 0;
  uint32_t next = next_offset(found);
  if (next < arena_size && !at(next)->used)
    block->bytes += sizeof(block_t) + at(next)->bytes;
  if (previous != UINT32_MAX && !at(previous)->used)
    at(previous)->bytes += sizeof(block_t) + block->bytes;
}
static int fheap_task_create(void (*function)(void *), const char *name,
                             unsigned stack, void *argument, int priority,
                             TaskHandle_t *task) {
  (void)function;
  (void)name;
  (void)argument;
  assert(stack == 8192 && priority == 4);
  *task = fheap_malloc(stack);
  if (*task)
    memset(*task, 0xa5, stack);
  return *task ? pdPASS : 0;
}
static void fheap_task_delete(TaskHandle_t task) { fheap_free(task); }
static int fheap_aac_decode(void *decoder, esp_audio_dec_in_raw_t *raw,
                            esp_audio_dec_out_frame_t *out,
                            esp_audio_dec_info_t *info) {
  (void)decoder;
  assert(raw->len == 7);
  aac_output_capacity = out->len;
  if (out->len >= aac_decoded_bytes)
    memset(out->buffer, 0, aac_decoded_bytes);
  out->decoded_size = aac_decoded_bytes;
  info->sample_rate = 48000;
  info->bits_per_sample = 16;
  info->channel = 2;
  return ESP_AUDIO_ERR_OK;
}
bool muse_wifi_connected(void) { return true; }
bool muse_settings_speaker_on(void) { return true; }
bool app_nabo_radio_network_begin(void) { return true; }
void app_nabo_radio_network_end(void) {}
void app_nabo_radio_store_clock(unsigned seconds) { (void)seconds; }
bool app_nabo_radio_active(void) { return nabo_radio_active(); }

void host_memory_trace_reset(void) {
  assert(live_reserved == baseline_reserved && live_blocks == baseline_blocks);
  memset(trace, 0, sizeof(trace));
  attempts = failures = injected_failures = 0;
  maximum_request = largest_failed_request = 0;
  peak_reserved = live_reserved;
  min_free = fheap_free_bytes();
  fail_index = -1;
}
bool host_memory_configure(unsigned profile) {
  if (s_decode || s_ring || s_network || nabo_radio_active())
    return false;
  static const unsigned holes[][8] = {
      {18432, 16384, 7680, 7680, 7680, 7680, 6144, 4352},
      {18432, 15872, 7936, 7680, 7680, 7680, 6144, 4608},
      {18432, 16384, 8192, 8192, 7680, 7680, 6144, 4096},
      {131072, 0, 0, 0, 0, 0, 0, 0},
      {16384, 18432, 7680, 7680, 7680, 7680, 6144, 4352}};
  if (profile >= sizeof(holes) / sizeof(holes[0]))
    return false;
  unsigned count = 0;
  size_t payload = 0;
  while (count < 8 && holes[profile][count])
    payload += holes[profile][count++];
  arena_size = (uint32_t)(payload + count * sizeof(block_t) +
                          (count - 1) * (32 + sizeof(block_t)));
  assert(arena_size <= ARENA_BYTES);
  memset(arena, 0, arena_size);
  *at(0) = (block_t){.bytes = arena_size - sizeof(block_t), .magic = MAGIC};
  live_reserved = peak_reserved = 0;
  live_blocks = bad_frees = 0;
  tracing = false;
  void *temporary[8] = {0};
  for (unsigned i = 0; i < count; ++i) {
    temporary[i] = fheap_malloc(holes[profile][i]);
    assert(temporary[i]);
    if (i + 1 < count)
      assert(fheap_malloc(32)); /* Surviving unrelated owner. */
  }
  for (unsigned i = 0; i < count; ++i)
    fheap_free(temporary[i]);
  baseline_reserved = live_reserved;
  baseline_blocks = live_blocks;
  initial_free = fheap_free_bytes();
  initial_largest = fheap_largest();
  tracing = true;
  host_memory_trace_reset();
  return true;
}
void host_memory_fail_at(int index) { fail_index = index; }
bool host_memory_begin(void) {
  nabo_radio_select_station(0);
  return nabo_radio_begin();
}
size_t host_memory_feed(const void *data, size_t bytes) {
  return s_ring ? ring_send(s_ring, data, bytes) : 0;
}
void host_memory_eof(void) { atomic_store(&s_done, true); }
size_t host_memory_read(int16_t *out, size_t frames) {
  return nabo_radio_read(out, frames, 0);
}
bool host_memory_finished(void) { return nabo_radio_finished(); }
bool host_memory_clean(void) {
  return !s_decode && !s_ring && !s_ring_bytes && !s_network &&
         !nabo_radio_active() && !bad_frees &&
         live_reserved == baseline_reserved && live_blocks == baseline_blocks &&
         fheap_free_bytes() == initial_free &&
         fheap_largest() == initial_largest;
}
bool host_memory_finish(void) {
  nabo_radio_stop();
  atomic_store(&s_done, true);
  nabo_radio_finish();
  return host_memory_clean();
}
uint64_t host_memory_stat(unsigned key) {
  switch (key) {
  case 0:
    return initial_free;
  case 1:
    return initial_largest;
  case 2:
    return fheap_free_bytes();
  case 3:
    return fheap_largest();
  case 4:
    return live_reserved - baseline_reserved;
  case 5:
    return peak_reserved - baseline_reserved;
  case 6:
    return live_blocks - baseline_blocks;
  case 7:
    return attempts;
  case 8:
    return failures;
  case 9:
    return injected_failures;
  case 10:
    return bad_frees;
  case 11:
    return maximum_request;
  case 12:
    return largest_failed_request;
  case 13:
    return s_ring_bytes;
  case 14:
    return sizeof(decode_t);
  case 15:
    return sizeof(SubbandInfo);
  case 16:
    return min_free;
  case 17:
    return arena_size;
  case 18:
    return sizeof(void *) * 8;
  default:
    return UINT64_MAX;
  }
}
uint64_t host_memory_allocation(unsigned index, unsigned field) {
  if (index >= attempts || index >= MAX_TRACE)
    return UINT64_MAX;
  allocation_t *allocation = &trace[index];
  switch (field) {
  case 0:
    return allocation->requested;
  case 1:
    return allocation->success;
  case 2:
    return allocation->free_before;
  case 3:
    return allocation->largest_before;
  case 4:
    return allocation->live_after;
  case 5:
    return allocation->injected;
  default:
    return UINT64_MAX;
  }
}
/* Same diagnostics exposed by the regular engine harness; keeps host-only
 * network helpers link-visible without pretending to run a network worker. */
int64_t host_memory_open_begin(void) { return telemetry_open_begin(); }
void host_memory_open_end(int64_t started) { telemetry_open_end(started); }
void host_memory_reconnect(unsigned reason) { telemetry_reconnect(reason); }
unsigned host_memory_stall(unsigned bytes, unsigned bitrate) {
  return network_stall_ms(bytes, bitrate);
}
int host_memory_aac_capacity(unsigned decoded_bytes) {
  assert(s_decode);
  static const uint8_t adts[7] = {0xff, 0xf1, 0x50, 0x80, 0x00, 0xe0, 0x00};
  memcpy(s_decode->input, adts, sizeof(adts));
  s_decode->used = sizeof(adts);
  s_decode->codec = 1;
  aac_decoded_bytes = decoded_bytes;
  nabo_radio_frame_info_t info = {0};
  return decode_frame(s_decode, &info);
}
unsigned host_memory_aac_output_capacity(void) { return aac_output_capacity; }
