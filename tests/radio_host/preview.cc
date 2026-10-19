/* Host harness uses the complete production scene and input code. Only the
 * hardware, network and NVS boundaries are replaced; no duplicated mock UI. */
#include "nabo_radio_view.h"
#include "radio_host_platform.h"
extern "C" {
#include "nabo_radio.h"
#include "nabo_radio_keys.h"
}
#include <cassert>
#include <set>
int64_t host_us = 1000000;
std::string host_assets;
uint16_t host_lcd[240 * 320];
int host_opened = -1;
static nrf_catalog_t catalog;
static int current = 0, local = -1, state = 0;
static bool wanted = false, loud = true, noise = true;
static unsigned sleep_time = 0;
static std::set<int> favorites;
extern "C" {
bool muse_wifi_connected() { return true; }
bool nabo_radio_bind_catalog(const void *p, size_t n) {
  return nrf_catalog_init(&catalog, p, n);
}
const nrf_catalog_t *nabo_radio_catalog() { return &catalog; }
bool nabo_radio_station_info(int i, nrf_station_t *s) {
  return nrf_catalog_get(&catalog, i, s);
}
int nabo_radio_station_count() { return (int)catalog.count; }
const char *nabo_radio_station_name(int i) {
  nrf_station_t s;
  return nrf_catalog_get(&catalog, i, &s) ? s.name : "";
}
const char *nabo_radio_category_name(int i) {
  return nrf_catalog_region(&catalog, (unsigned)i);
}
int nabo_radio_category_count() { return (int)catalog.regions; }
int nabo_radio_current_station() { return current; }
int nabo_radio_local() { return local; }
void nabo_radio_set_local(int i) { local = i; }
int nabo_radio_recent(int i) { return i < 3 ? i : -1; }
bool nabo_radio_favorite(int i) { return favorites.count(i) != 0; }
void nabo_radio_toggle_favorite(int i) {
  if (!favorites.erase(i))
    favorites.insert(i);
}
bool nabo_radio_wanted() { return wanted; }
unsigned nabo_radio_state() { return (unsigned)state; }
const char *nabo_radio_state_text() {
  return state == 3 ? "播放中" : state == 1 ? "连接中…" : "按 OK 收听";
}
void nabo_radio_select_station(int i) {
  current = i;
  wanted = true;
  state = 1;
}
void nabo_radio_toggle_play() {
  wanted = !wanted;
  state = wanted ? 1 : 7;
}
void nabo_radio_set_sleep(unsigned n) { sleep_time = n * 60; }
unsigned nabo_radio_sleep_remaining() { return sleep_time; }
int nabo_radio_meter() { return state == 3 ? 160 : 0; }
bool nabo_radio_loudness() { return loud; }
void nabo_radio_set_loudness(bool on) { loud = on; }
bool nabo_radio_noise() { return noise; }
void nabo_radio_set_noise(bool on) { noise = on; }
}
static void advance(unsigned ms) {
  host_us += (int64_t)ms * 1000;
  nabo_radio_view_tick(76);
}
static void edge(int k, bool down) {
  nabo_radio_view_edge(k, down, (uint32_t)(host_us / 1000));
}
static void click(int k) {
  edge(k, true);
  advance(40);
  edge(k, false);
  advance(30);
}
static void single(int k) {
  click(k);
  advance(270);
}
static void hold(int k) {
  edge(k, true);
  advance(670);
  edge(k, false);
  advance(30);
}
static void save(const std::string &dir, const char *name) {
  nabo_radio_view_tick(76);
  std::ofstream f(dir + "/" + name + ".rgb565", std::ios::binary);
  f.write((char *)host_lcd, sizeof(host_lcd));
}
int main(int argc, char **argv) {
  if (argc != 3)
    return 2;
  host_assets = argv[1];
  std::string output = argv[2];
  assert(nabo_radio_view_open());
  assert(catalog.count == 1105);
  advance(1000);
  save(output, "player");
  single(NRF_OK);
  state = 3;
  advance(3000);
  save(output, "playing");
  // Local settings hide the panel but retain the large sprite workspace so
  // reopening the player never needs a new large allocation during playback.
  nabo_radio_view_hide();
  assert(nabo_radio_view_open());
  assert(wanted && state == 3);
  advance(40);
  int previous = current;
  click(NRF_DOWN);
  click(NRF_DOWN);
  click(NRF_DOWN);
  assert(current == previous);
  advance(501);
  assert(current == previous + 3);
  click(NRF_OK);
  click(NRF_OK);
  assert(favorites.count(current));
  assert(wanted);
  advance(300);
  save(output, "favorite");
  hold(NRF_OK);
  save(output, "menu");
  single(NRF_OK);
  save(output, "list");
  hold(NRF_OK);
  save(output, "favorites-list");
  click(NRF_UP);
  click(NRF_UP);
  advance(300);
  hold(NRF_OK);
  single(NRF_DOWN);
  single(NRF_OK);
  save(output, "search");
  single(NRF_OK);
  save(output, "search-a");
  click(NRF_UP);
  click(NRF_UP);
  advance(300);
  click(NRF_UP);
  click(NRF_UP);
  advance(300);
  // A double UP on the player returns and rolls back its speculative move.
  click(NRF_UP);
  click(NRF_UP);
  advance(300);
  assert(host_opened == NABO_TOOLS);
  nabo_radio_view_close();
  assert(nabo_radio_view_open());
  state = 3;
  advance(100);
  advance(61000);
  save(output, "clock");
  int before = current;
  click(NRF_DOWN);
  advance(600);
  assert(current == before);
  save(output, "awakened");
  nabo_radio_view_close();
  puts("PASS: production FM scene; 1105 catalog entries, deferred scan, "
       "favorite double click, hold menu, nested back, wake suppression");
}
