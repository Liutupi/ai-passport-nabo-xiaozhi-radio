#pragma once
#include <stdint.h>
bool nabo_radio_view_open();
void nabo_radio_view_close();
void nabo_radio_view_tick(int battery);
void nabo_radio_view_edge(int key, bool pressed, uint32_t event_ms);
bool nabo_radio_view_back();
