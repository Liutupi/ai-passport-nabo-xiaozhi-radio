#pragma once
#include "lvgl.h"
#include "muse_menu.h"
enum NaboApp {
    NABO_HOME,
    NABO_TOOLS,
    NABO_SETTINGS,
    NABO_RADIO,
    NABO_STATUS,
    NABO_BATTERY,
    NABO_WIFI
};
void nabo_apps_build(lv_obj_t* screen);
void nabo_apps_tick();
bool nabo_apps_key(muse_menu_key_t key);
void nabo_apps_charge(bool pressed, uint32_t event_ms = 0);
void nabo_apps_open(NaboApp app, int delay_ms = 0);
bool nabo_apps_active();
bool nabo_apps_is_jump();
/* Local game that needs a fast display refresh (jump). */
bool nabo_apps_is_fast();
/* Raw DOWN press edge (unused by the menu radio). */
void nabo_apps_down_pressed(uint32_t event_ms);
/* Raw DOWN release edge. */
void nabo_apps_down_released(uint32_t event_ms);
// Raw UP edges.
void nabo_apps_up_edge(bool pressed, uint32_t event_ms);
void nabo_apps_register_tools();
void nabo_apps_initialize();
bool nabo_apps_voice_command(const char* transcript);

void nabo_apps_record_frame();
