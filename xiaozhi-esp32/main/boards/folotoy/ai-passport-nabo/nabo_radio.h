#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "nabo_radio_catalog.h"

bool nabo_radio_bind_catalog(const void* data, size_t size);
const nrf_catalog_t* nabo_radio_catalog(void);
bool nabo_radio_station_info(int index, nrf_station_t* out);
int nabo_radio_recent(int position);
void nabo_radio_set_sleep(unsigned minutes);
unsigned nabo_radio_sleep_remaining(void);
void nabo_radio_set_loudness(bool enabled);
bool nabo_radio_loudness(void);
unsigned nabo_radio_output_rate(void);
int nabo_radio_meter(void);
unsigned nabo_radio_state(void);
void nabo_radio_set_local(int region);
int nabo_radio_local(void);
void nabo_radio_set_noise(bool enabled);
bool nabo_radio_noise(void);

/* UI calls publish intent only; the existing voice task owns speaker writes. */
int nabo_radio_station_count(void);
const char* nabo_radio_station_name(int index);
int nabo_radio_station_category(int index);
int nabo_radio_category_count(void);
const char* nabo_radio_category_name(int index);
int nabo_radio_current_station(void);
bool nabo_radio_favorite(int index);
void nabo_radio_toggle_favorite(int index);
void nabo_radio_select_station(int index);
void nabo_radio_toggle_play(void);
void nabo_radio_stop(void);
bool nabo_radio_wanted(void);
bool nabo_radio_active(void);
bool nabo_radio_available(void);
const char* nabo_radio_state_text(void);

/* Called only by the voice owner, never from LVGL or a network callback. */
void nabo_radio_init(void);
void nabo_radio_idle_tick(void);
bool nabo_radio_due(void);
bool nabo_radio_begin(void);
size_t nabo_radio_read(int16_t* pcm, size_t frames, int wait_ms);
bool nabo_radio_finished(void);
void nabo_radio_finish(void);

/* app.c owns Muse's network gate and frees/restores its TLS/Noise session. */
bool app_nabo_radio_network_begin(void);
void app_nabo_radio_network_end(void);
bool app_nabo_radio_active(void);
