#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <string>
#include <vector>
using esp_lcd_panel_io_handle_t = void *;
using esp_lcd_panel_handle_t = void *;
inline tm *localtime_r(const time_t *t, tm *out) {
  localtime_s(out, t);
  return out;
}
extern int64_t host_us;
extern std::string host_assets;
extern uint16_t host_lcd[240 * 320];
extern int host_opened;
constexpr int MALLOC_CAP_INTERNAL = 1, MALLOC_CAP_DMA = 2;
enum { NABO_HOME, NABO_TOOLS, NABO_SETTINGS };
inline int64_t esp_timer_get_time() { return host_us; }
inline uint32_t esp_random() { return 123456789; }
inline void *heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void heap_caps_free(void *p) { free(p); }
inline void nabo_apps_open(int app, int = 0) { host_opened = app; }
#define ESP_LOGE(tag, ...)                                                     \
  do {                                                                         \
    fprintf(stderr, "%s: ", tag);                                              \
    fprintf(stderr, __VA_ARGS__);                                              \
    fputc('\n', stderr);                                                       \
  } while (0)
class Assets {
  std::map<std::string, std::vector<uint8_t>> files;

public:
  static Assets &GetInstance() {
    static Assets a;
    return a;
  }
  bool GetAssetData(const std::string &name, void *&ptr, size_t &size) {
    auto &v = files[name];
    if (v.empty()) {
      std::ifstream f(host_assets + "/" + name, std::ios::binary);
      v.assign(std::istreambuf_iterator<char>(f), {});
    }
    ptr = v.data();
    size = v.size();
    return size != 0;
  }
};
class NaboDisplay {
public:
  void BeginDirectDraw(void **io, void **panel) {
    *io = this;
    *panel = this;
  }
  void EndDirectDraw() {}
};
class Board {
  NaboDisplay display;
  class Light {
    int value = 80;

  public:
    int brightness() { return value; }
    void SetBrightness(int n, bool) { value = n; }
  } light;

public:
  static Board &GetInstance() {
    static Board b;
    return b;
  }
  NaboDisplay *GetDisplay() { return &display; }
  Light *GetBacklight() { return &light; }
};
inline int esp_lcd_panel_io_tx_param(void *, int, const void *, size_t) {
  return 0;
}
inline int esp_lcd_panel_draw_bitmap(void *, int x, int y, int x2, int y2,
                                     const void *data) {
  const auto *p = static_cast<const uint16_t *>(data);
  for (int yy = y; yy < y2; yy++)
    for (int xx = x; xx < x2; xx++) {
      uint16_t c = *p++;
      host_lcd[yy * 240 + xx] = static_cast<uint16_t>((c >> 8) | (c << 8));
    }
  return 0;
}
