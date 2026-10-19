#define NRF_ENGINE_HOST
#include "nabo_radio.c"
int64_t host_engine_us = 1000000;
size_t host_ring_limit = 32768;
bool muse_wifi_connected(void) { return true; }
bool muse_settings_speaker_on(void) { return true; }
bool app_nabo_radio_network_begin(void) { return true; }
void app_nabo_radio_network_end(void) {}
bool app_nabo_radio_active(void) { return nabo_radio_active(); }
size_t host_feed(const void *data, size_t n) {
  return xStreamBufferSend(s_ring, data, n, 0);
}
void host_eof(void) { atomic_store(&s_done, true); }
bool host_clean(void) {
  return !s_decode && !s_ring && !s_ring_bytes && !s_network && !nabo_radio_active();
}
void host_limit_ring(unsigned n) { host_ring_limit = n; }
unsigned host_ring_capacity(void) { return (unsigned)s_ring_bytes; }
void host_advance(unsigned ms) {
  host_engine_us += (int64_t)ms * 1000;
  nabo_radio_idle_tick();
}
