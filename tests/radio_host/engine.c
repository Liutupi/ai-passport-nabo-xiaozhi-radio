#define NRF_ENGINE_HOST
#include "nabo_radio.c"
#ifndef NRF_BASELINE
#include "nabo_radio_audio.h"
#endif
int64_t host_engine_us = 1000000;
size_t host_ring_limit = 32768;
int host_ring_fail_after = -1, host_ring_allocated;
size_t host_pipeline_heap = 256 * 1024, host_pipeline_largest = 256 * 1024;
unsigned host_saved_clock = 60;
void app_nabo_radio_store_clock(unsigned seconds) {
  host_saved_clock = seconds;
}
unsigned host_stored_clock(void) { return host_saved_clock; }
void host_heap_budget(unsigned available, unsigned largest) {
  host_pipeline_heap = available;
  host_pipeline_largest = largest;
}
bool muse_wifi_connected(void) { return true; }
bool muse_settings_speaker_on(void) { return true; }
bool app_nabo_radio_network_begin(void) { return true; }
void app_nabo_radio_network_end(void) {}
bool app_nabo_radio_active(void) { return nabo_radio_active(); }
size_t host_feed(const void *data, size_t n) {
  return ring_send(s_ring, data, n);
}
void host_eof(void) { atomic_store(&s_done, true); }
bool host_clean(void) {
  return !s_decode && !s_ring && !s_ring_bytes && !s_network && !host_ring_allocated && !nabo_radio_active();
}
void host_limit_ring(unsigned n) { host_ring_limit = n; }
unsigned host_ring_capacity(void) { return (unsigned)s_ring_bytes; }
void host_fail_ring_after(int n) { host_ring_fail_after = n; }
void host_wrap_positions(void) {
  atomic_store(&s_ring->written, UINT32_MAX - 1023);
  atomic_store(&s_ring->consumed, UINT32_MAX - 1023);
}
static radio_ring_t *host_probe;
bool host_probe_begin(unsigned n) { host_probe = ring_create(n); return host_probe != NULL; }
size_t host_probe_write(const void *p, size_t n) { return ring_send(host_probe, p, n); }
size_t host_probe_read(void *p, size_t n) { return ring_receive(host_probe, p, n); }
void host_probe_end(void) { ring_delete(host_probe); host_probe = NULL; }
void host_advance(unsigned ms) {
  host_engine_us += (int64_t)ms * 1000;
  nabo_radio_idle_tick();
}
#ifndef NRF_BASELINE
int64_t host_open_begin(void) { return telemetry_open_begin(); }
void host_open_end(int64_t started) { telemetry_open_end(started); }
void host_reconnect(unsigned reason) { telemetry_reconnect(reason); }
unsigned host_stall_ms(unsigned buffered, unsigned bitrate_kbps) {
  return network_stall_ms(buffered, bitrate_kbps);
}
void host_schedule_retry(unsigned delay_ms) { schedule_retry(delay_ms); }
void host_allocation_backoff(void) { allocation_backoff(); }
unsigned host_retry_deadline(void) { return atomic_load(&s_retry_at); }
void host_memory_release(void) {
  atomic_store(&s_release_for_memory, true);
  atomic_store(&s_done, true);
}
static nrf_audio_tail_t host_audio_tail;
void host_audio_reset(void) { memset(&host_audio_tail, 0, sizeof(host_audio_tail)); }
void host_audio_smooth(int16_t *pcm, unsigned decoded, unsigned count, unsigned rate) {
  nrf_audio_smooth(&host_audio_tail, pcm, decoded, count, rate);
}
#endif
