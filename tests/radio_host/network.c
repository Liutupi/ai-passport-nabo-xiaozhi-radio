/* Runs the production network_worker to completion against a scripted
 * transport. There are no sockets, threads, sleeps, serial ports or device
 * dependencies.
 */
#define NRF_ENGINE_HOST
#define NRF_NETWORK_HOST
#include "radio_engine_platform.h"
#include "radio_network_platform.h"

/* HTTP types must be visible before the included production implementation. */
#include "nabo_radio.c"

#include <assert.h>
#include <setjmp.h>

enum {
  NET_COMPLETE = 1,
  NET_WIFI_DOWN = 2,
  NET_CANCEL = 4,
  NET_WIFI_UP = 8,
  NET_MEMORY_OK = 16,
  NET_SWITCH = 32,
  NET_DRAIN = 64
};
enum {
  MAX_CONNECTIONS = 24,
  MAX_READS = 256,
  MAX_READ_CALLS = MAX_READS * 4,
  MAX_CONTROLS = 32,
  MAX_DATA = 256 * 1024,
  NETWORK_TELEMETRY_WORDS = 14
};
typedef struct {
  int result, error;
  unsigned ms, flags;
} read_step_t;
struct host_http_session {
  bool init_ok, complete, positive_snapshot_taken;
  int open_result, status, transport_errno;
  int socket_fd, timeout_set_result, socket_timeout_result;
  int64_t header_length;
  unsigned init_ms, open_ms, fetch_ms, timeout;
  unsigned read_count, reads_at, cleanup_count, close_count, errno_calls,
      complete_calls, bytes;
  unsigned get_socket_calls, timeout_set_calls, socket_timeout_calls,
      socket_timeout, socket_timeout_requested, actual_read_calls;
  unsigned read_offset, callback_fragment, callback_tail_delay, callback_count,
      callback_bytes, icy_metaint;
  char content_type[64];
  uint64_t init_at, open_at, first_read_at, cleanup_at, close_at;
  read_step_t reads[MAX_READS];
  unsigned read_capacities[MAX_READ_CALLS];
  unsigned before_return[NETWORK_TELEMETRY_WORDS];
  unsigned after_callbacks[NETWORK_TELEMETRY_WORDS];
};
struct host_http_client {
  bool alive;
  struct host_http_session *current;
  esp_http_client_config_t config;
};
typedef struct {
  unsigned at, flags;
  bool done;
} control_t;

int64_t host_engine_us = 1000000;
size_t host_ring_limit = 32768;
int host_ring_fail_after = -1, host_ring_allocated;
size_t host_pipeline_heap = 256 * 1024, host_pipeline_largest = 256 * 1024;
static struct host_http_session sessions[MAX_CONNECTIONS];
static struct host_http_client clients[MAX_CONNECTIONS];
static unsigned connection_count, init_count, open_count, fetch_count,
    read_count;
static unsigned cleanup_count, close_count, delay_count, unexpected_count,
    duplicate_cleanup;
static unsigned next_attempt;
static unsigned controls_count, data_size, data_at, run_limit;
static uint8_t producer_data[MAX_DATA];
static control_t controls[MAX_CONTROLS];
static int64_t start_us;
static bool wifi = true, running;
static jmp_buf worker_finished;
static radio_ring_t *original_ring;
static decode_t *original_decode;
static TaskHandle_t original_network;
static decode_t decoder_snapshot;
static uint8_t catalog_data[128];

static void network_snapshot(unsigned *words) {
#ifndef NRF_NETWORK_BEFORE
  nabo_radio_network_telemetry_t snapshot;
  _Static_assert(sizeof(snapshot) == NETWORK_TELEMETRY_WORDS * sizeof(unsigned),
                 "network telemetry ABI changed");
  nabo_radio_network_telemetry(&snapshot);
  memcpy(words, &snapshot, sizeof(snapshot));
#else
  memset(words, 0, NETWORK_TELEMETRY_WORDS * sizeof(*words));
#endif
}

static unsigned elapsed_ms(void) {
  return (unsigned)((host_engine_us - start_us) / 1000);
}
static void apply_flags(unsigned flags) {
  if (flags & NET_WIFI_DOWN)
    wifi = false;
  if (flags & NET_WIFI_UP)
    wifi = true;
  if (flags & NET_CANCEL)
    atomic_store(&s_wanted, false);
  if (flags & NET_SWITCH)
    nabo_radio_select_station(0);
  if (flags & NET_DRAIN) {
    uint8_t discard[1024];
    while (ring_receive(s_ring, discard, sizeof(discard))) {
    }
  }
  if (flags & NET_MEMORY_OK) {
    host_pipeline_heap = host_pipeline_largest = 256 * 1024;
  }
}
static void advance(unsigned ms) {
  host_engine_us += (int64_t)ms * 1000;
  for (unsigned i = 0; i < controls_count; ++i) {
    if (!controls[i].done && elapsed_ms() >= controls[i].at) {
      controls[i].done = true;
      apply_flags(controls[i].flags);
    }
  }
  if (running && elapsed_ms() >= run_limit) {
    ++unexpected_count;
    atomic_store(&s_wanted, false);
  }
}
bool muse_wifi_connected(void) { return wifi; }
bool muse_settings_speaker_on(void) { return true; }
bool app_nabo_radio_network_begin(void) { return true; }
void app_nabo_radio_network_end(void) {}
void app_nabo_radio_store_clock(unsigned seconds) { (void)seconds; }
bool app_nabo_radio_active(void) { return nabo_radio_active(); }
int esp_crt_bundle_attach(void *ignored) {
  (void)ignored;
  return ESP_OK;
}
void vTaskDelay(unsigned ticks) {
  ++delay_count;
  advance(ticks ? ticks : 1);
}
void vTaskSuspend(TaskHandle_t task) {
  (void)task;
  assert(running);
  longjmp(worker_finished, 1);
}
esp_http_client_handle_t
esp_http_client_init(const esp_http_client_config_t *config) {
  unsigned client_index = init_count++;
  if (next_attempt >= connection_count || client_index >= MAX_CONNECTIONS) {
    ++unexpected_count;
    atomic_store(&s_wanted, false);
    return NULL;
  }
  struct host_http_session *session = &sessions[next_attempt];
  session->init_at = elapsed_ms();
  advance(session->init_ms);
  if (!session->init_ok) {
    ++next_attempt;
    return NULL;
  }
  struct host_http_client *client = &clients[client_index];
  client->config = *config;
  client->alive = true;
  return client;
}
int esp_http_client_set_header(esp_http_client_handle_t http, const char *key,
                               const char *value) {
  assert(http && http->alive && key && value);
  return ESP_OK;
}
int esp_http_client_open(esp_http_client_handle_t http, int length) {
  (void)length;
  assert(http && http->alive);
  ++open_count;
  if (next_attempt >= connection_count) {
    ++unexpected_count;
    atomic_store(&s_wanted, false);
    return -1;
  }
  http->current = &sessions[next_attempt++];
  http->current->open_at = elapsed_ms();
  advance(http->current->open_ms);
  return http->current->open_result;
}
int64_t esp_http_client_fetch_headers(esp_http_client_handle_t http) {
  assert(http && http->alive);
  ++fetch_count;
  advance(http->current->fetch_ms);
  if (http->config.event_handler) {
    esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_HEADER,
                                     .client = http,
                                     .user_data = http->config.user_data,
                                     .header_key = "Content-Type",
                                     .header_value =
                                         http->current->content_type};
    assert(http->config.event_handler(&event) == ESP_OK);
    if (http->current->icy_metaint) {
      char interval[32];
      snprintf(interval, sizeof(interval), "%u", http->current->icy_metaint);
      event.header_key = "icy-metaint";
      event.header_value = interval;
      assert(http->config.event_handler(&event) == ESP_OK);
    }
  }
  return http->current->header_length;
}
int esp_http_client_get_status_code(esp_http_client_handle_t http) {
  return http->current ? http->current->status : 0;
}
int esp_http_client_set_redirection(esp_http_client_handle_t http) {
  (void)http;
  return -1;
}
int esp_http_client_close(esp_http_client_handle_t http) {
  ++close_count;
  if (http->current) {
    ++http->current->close_count;
    http->current->close_at = elapsed_ms();
  }
  return ESP_OK;
}
int esp_http_client_set_timeout_ms(esp_http_client_handle_t http, int timeout) {
  assert(http && http->alive && http->current);
  ++http->current->timeout_set_calls;
  http->current->timeout = (unsigned)timeout;
  return http->current->timeout_set_result;
}
int esp_http_client_get_socket(esp_http_client_handle_t http) {
  assert(http && http->alive && http->current);
  ++http->current->get_socket_calls;
  return http->current->socket_fd;
}
int radio_socket_receive_timeout(int fd, unsigned timeout_ms) {
  for (unsigned i = 0; i < init_count && i < MAX_CONNECTIONS; ++i) {
    struct host_http_client *client = &clients[i];
    if (client->alive && client->current && client->current->socket_fd == fd) {
      struct host_http_session *session = client->current;
      ++session->socket_timeout_calls;
      session->socket_timeout_requested = timeout_ms;
      if (session->socket_timeout_result == 0)
        session->socket_timeout = timeout_ms;
      return session->socket_timeout_result;
    }
  }
  ++unexpected_count;
  return -1;
}
static void body_callbacks(esp_http_client_handle_t http, char *data,
                           unsigned bytes) {
  if (!http->config.event_handler)
    return;
  struct host_http_session *session = http->current;
  unsigned fragment = session->callback_fragment;
  if (!fragment)
    fragment = bytes;
  for (unsigned at = 0; at < bytes;) {
    unsigned count = bytes - at;
    if (count > fragment)
      count = fragment;
    esp_http_client_event_t event = {.event_id = HTTP_EVENT_ON_DATA,
                                     .client = http,
                                     .user_data = http->config.user_data,
                                     .data = data + at,
                                     .data_len = (int)count};
    ++session->callback_count;
    session->callback_bytes += count;
    assert(http->config.event_handler(&event) == ESP_OK);
    at += count;
  }
}
int esp_http_client_read(esp_http_client_handle_t http, char *data,
                         int capacity) {
  assert(http && http->alive && capacity > 0);
  struct host_http_session *session = http->current;
  assert(session);
  ++read_count;
  unsigned call_index = session->actual_read_calls++;
  assert(call_index < MAX_READ_CALLS);
  session->read_capacities[call_index] = (unsigned)capacity;
  if (!call_index)
    session->first_read_at = elapsed_ms();
  if (session->reads_at >= session->read_count) {
    ++unexpected_count;
    atomic_store(&s_wanted, false);
    errno = session->transport_errno = 0;
    return 0;
  }
  read_step_t step = session->reads[session->reads_at];
  bool first_piece = session->read_offset == 0;
  if (first_piece)
    advance(step.ms);
  errno = session->transport_errno = step.error;
  if (step.result > 0) {
    unsigned count = (unsigned)step.result - session->read_offset;
    if (count > (unsigned)capacity)
      count = (unsigned)capacity;
    assert(data_at + count <= data_size);
    memcpy(data, producer_data + data_at, count);
    data_at += count;
    session->bytes += count;
    session->read_offset += count;
    body_callbacks(http, data, count);
    if (!session->positive_snapshot_taken)
      network_snapshot(session->after_callbacks);
    if (first_piece)
      advance(session->callback_tail_delay);
    if (!session->positive_snapshot_taken) {
      network_snapshot(session->before_return);
      session->positive_snapshot_taken = true;
    }
    if (session->read_offset == (unsigned)step.result) {
      session->read_offset = 0;
      ++session->reads_at;
      apply_flags(step.flags);
      session->complete = (step.flags & NET_COMPLETE) != 0;
    }
    return (int)count;
  }
  ++session->reads_at;
  apply_flags(step.flags);
  session->complete = (step.flags & NET_COMPLETE) != 0;
  return step.result;
}
int esp_http_client_get_errno(esp_http_client_handle_t http) {
  ++http->current->errno_calls;
  return http->current->transport_errno;
}
bool esp_http_client_is_complete_data_received(esp_http_client_handle_t http) {
  ++http->current->complete_calls;
  return http->current->complete;
}
int esp_http_client_cleanup(esp_http_client_handle_t http) {
  assert(http);
  ++cleanup_count;
  if (!http->alive)
    ++duplicate_cleanup;
  http->alive = false;
  if (http->current) {
    ++http->current->cleanup_count;
    http->current->cleanup_at = elapsed_ms();
  }
  return ESP_OK;
}

bool host_net_reset_station(unsigned capacity, unsigned qt) {
  if (s_decode || s_ring || s_network || host_ring_allocated)
    return false;
  memset(sessions, 0, sizeof(sessions));
  memset(clients, 0, sizeof(clients));
  memset(controls, 0, sizeof(controls));
  connection_count = init_count = open_count = fetch_count = read_count = 0;
  cleanup_count = close_count = delay_count = unexpected_count =
      duplicate_cleanup = next_attempt = 0;
  controls_count = data_size = data_at = 0;
  wifi = true;
  host_pipeline_heap = host_pipeline_largest = 256 * 1024;
  start_us = host_engine_us;
  host_ring_limit = capacity;
  /* A real, minimal catalog makes qt identity explicit, independent of the
   * legacy fallback table or any production catalog/file/network access. */
  memset(catalog_data, 0, sizeof(catalog_data));
  memcpy(catalog_data, "NFC1", 4);
  catalog_data[4] = catalog_data[6] = 1;
  catalog_data[8] = 48;
  catalog_data[16] = 1;
  catalog_data[20] = 1;
  catalog_data[24] = 6;
  for (unsigned i = 0; i < 4; ++i)
    catalog_data[36 + i] = (uint8_t)(qt >> (8 * i));
  static const char strings[] = "\0host\0http://fixture.invalid/live";
  memcpy(catalog_data + 48, strings, sizeof(strings));
  if (!nrf_catalog_init(&s_catalog, catalog_data, 48 + sizeof(strings)))
    return false;
  nabo_radio_select_station(0);
  return nabo_radio_begin() && s_ring_bytes == capacity;
}
bool host_net_reset(unsigned capacity) {
  return host_net_reset_station(capacity, 0);
}
int host_net_connection(int init_ok, int open_result, int status,
                        int64_t header_length, unsigned init_ms,
                        unsigned open_ms, unsigned fetch_ms) {
  if (connection_count >= MAX_CONNECTIONS)
    return -1;
  struct host_http_session *session = &sessions[connection_count];
  session->init_ok = init_ok != 0;
  session->open_result = open_result;
  session->status = status;
  session->header_length = header_length;
  session->init_ms = init_ms;
  session->open_ms = open_ms;
  session->fetch_ms = fetch_ms;
  session->socket_fd = 50 + (int)connection_count;
  snprintf(session->content_type, sizeof(session->content_type), "audio/mpeg");
  return (int)connection_count++;
}
bool host_net_timeout_setup(unsigned index, int fd, int setter_result,
                            int socket_result) {
  if (index >= connection_count)
    return false;
  sessions[index].socket_fd = fd;
  sessions[index].timeout_set_result = setter_result;
  sessions[index].socket_timeout_result = socket_result;
  return true;
}
bool host_net_body_callbacks(unsigned index, unsigned fragment_bytes,
                             unsigned tail_delay_ms) {
  if (index >= connection_count)
    return false;
  sessions[index].callback_fragment = fragment_bytes;
  sessions[index].callback_tail_delay = tail_delay_ms;
  return true;
}
bool host_net_response_headers(unsigned index, const char *content_type,
                               unsigned icy_metaint) {
  if (index >= connection_count || !content_type ||
      strlen(content_type) >= sizeof(sessions[index].content_type))
    return false;
  snprintf(sessions[index].content_type, sizeof(sessions[index].content_type),
           "%s", content_type);
  sessions[index].icy_metaint = icy_metaint;
  return true;
}
unsigned host_net_read_capacity(unsigned index, unsigned call_index) {
  if (index >= connection_count ||
      call_index >= sessions[index].actual_read_calls)
    return 0;
  return sessions[index].read_capacities[call_index];
}
void host_net_network_telemetry(unsigned *fourteen_values) {
  if (fourteen_values)
    network_snapshot(fourteen_values);
}
bool host_net_read_snapshot(unsigned index, unsigned phase,
                            unsigned *fourteen_values) {
  if (!fourteen_values || index >= connection_count ||
      !sessions[index].positive_snapshot_taken || phase > 1)
    return false;
  memcpy(fourteen_values,
         phase ? sessions[index].before_return
               : sessions[index].after_callbacks,
         NETWORK_TELEMETRY_WORDS * sizeof(*fourteen_values));
  return true;
}
bool host_net_add_read(unsigned index, int result, int error, unsigned ms,
                       unsigned flags) {
  if (index >= connection_count || sessions[index].read_count >= MAX_READS ||
      result > 1024)
    return false;
  struct host_http_session *session = &sessions[index];
  session->reads[session->read_count++] =
      (read_step_t){result, error, ms, flags};
  return true;
}
bool host_net_schedule(unsigned at_ms, unsigned flags) {
  if (controls_count >= MAX_CONTROLS)
    return false;
  controls[controls_count++] = (control_t){at_ms, flags, false};
  return true;
}
bool host_net_data(const void *data, unsigned size) {
  if (size > MAX_DATA)
    return false;
  memcpy(producer_data, data, size);
  data_size = size;
  return true;
}
void host_net_heap(unsigned free_bytes, unsigned largest) {
  host_pipeline_heap =
      free_bytes + (size_t)host_ring_allocated * (RING_BLOCK_BYTES + 8);
  host_pipeline_largest = largest;
}
size_t host_net_seed(const void *data, size_t size) {
  return ring_send(s_ring, data, size);
}
size_t host_net_decode(int16_t *samples, unsigned count) {
  return nabo_radio_read(samples, count, 0);
}
unsigned host_net_copy_ring(void *data, unsigned capacity) {
  unsigned available = (unsigned)ring_available(s_ring);
  if (available > capacity)
    available = capacity;
  unsigned consumed = atomic_load(&s_ring->consumed);
  for (unsigned i = 0; i < available; ++i) {
    unsigned position = (consumed + i) % (unsigned)s_ring_bytes;
    ((uint8_t *)data)[i] =
        s_ring->block[position / RING_BLOCK_BYTES][position % RING_BLOCK_BYTES];
  }
  return available;
}
bool host_net_run(unsigned maximum_ms) {
  run_limit = maximum_ms;
  original_ring = s_ring;
  original_decode = s_decode;
  original_network = s_network;
  decoder_snapshot = *s_decode;
  running = true;
  if (!setjmp(worker_finished))
    network_worker((void *)(uintptr_t)s_audio_generation);
  running = false;
  return atomic_load(&s_done);
}
uint64_t host_net_value(unsigned key, unsigned index) {
  switch (key) {
  case 0:
    return init_count;
  case 1:
    return open_count;
  case 2:
    return fetch_count;
  case 3:
    return read_count;
  case 4:
    return cleanup_count;
  case 5:
    return delay_count;
  case 6:
    return elapsed_ms();
  case 7:
    return unexpected_count;
  case 8:
    return !atomic_load(&s_wanted);
  case 9:
    return original_ring == s_ring;
  case 10:
    return original_decode == s_decode &&
           !memcmp(&decoder_snapshot, s_decode, sizeof(decoder_snapshot));
  case 11: {
    unsigned alive = 0;
    for (unsigned i = 0; i < init_count && i < MAX_CONNECTIONS; ++i)
      alive += clients[i].alive;
    return alive;
  }
  case 12:
    return atomic_load(&s_done);
  case 13:
    return ring_available(s_ring);
  case 14:
    return nabo_radio_state();
  case 15:
    return duplicate_cleanup;
  case 16:
    return close_count;
  case 17:
    return original_network == s_network;
  case 18:
#ifndef NRF_NETWORK_BEFORE
    return atomic_load(&s_release_for_memory);
#else
    return 0;
#endif
  case 19:
    return atomic_load(&s_retry_at) - (uint32_t)(esp_timer_get_time() / 1000);
  case 20:
    return s_decode ? s_decode->ready - s_decode->at : 0;
  default:
    break;
  }
  if (index >= connection_count)
    return UINT64_MAX;
  struct host_http_session *session = &sessions[index];
  switch (key) {
  case 100:
    return session->init_at;
  case 101:
    return session->open_at;
  case 102:
    return session->first_read_at;
  case 103:
    return session->cleanup_at;
  case 104:
    return session->reads_at;
  case 105:
    return session->cleanup_count;
  case 106:
    return session->timeout;
  case 107:
    return session->complete_calls;
  case 108:
    return session->errno_calls;
  case 109:
    return session->bytes;
  case 110:
    return session->close_count;
  case 111:
    return session->close_at;
  case 112:
    return session->get_socket_calls;
  case 113:
    return session->timeout_set_calls;
  case 114:
    return session->socket_timeout_calls;
  case 115:
    return session->socket_timeout;
  case 116:
    return session->actual_read_calls;
  case 117:
    return session->callback_count;
  case 118:
    return session->callback_bytes;
  case 119:
    return session->socket_timeout_requested;
  default:
    return UINT64_MAX;
  }
}
bool host_net_finish(void) {
  nabo_radio_stop();
  atomic_store(&s_done, true);
  nabo_radio_finish();
  return !s_decode && !s_ring && !s_network && !s_ring_bytes &&
         !host_ring_allocated;
}
