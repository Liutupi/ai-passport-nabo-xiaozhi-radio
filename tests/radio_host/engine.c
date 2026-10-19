#define NRF_ENGINE_HOST
#include "nabo_radio.c"
int64_t host_engine_us = 1000000;
size_t host_ring_limit = 32768;
int host_ring_fail_after = -1, host_ring_allocated;
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
