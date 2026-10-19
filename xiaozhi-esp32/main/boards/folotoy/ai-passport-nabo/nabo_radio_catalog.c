#include "nabo_radio_catalog.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static unsigned u16(const uint8_t* p) { return p[0] | ((unsigned)p[1] << 8); }
static uint32_t u32(const uint8_t* p) {
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static const char* str(const nrf_catalog_t* c, uint32_t at) {
    if (at >= c->size - c->strings)
        return NULL;
    const char* s = (const char*)c->data + c->strings + at;
    return memchr(s, 0, c->size - c->strings - at) ? s : NULL;
}
const char* nrf_catalog_type(unsigned i) {
    static const char* const names[] = {"新闻", "音乐", "交通", "财经", "文艺", "生活",
                                        "体育", "方言", "综合", "戏曲", "其他"};
    return i < NRF_TYPES ? names[i] : "";
}
bool nrf_catalog_get(const nrf_catalog_t* c, int i, nrf_station_t* s) {
    if (!c || !c->data || !s || i < 0 || (unsigned)i >= c->count)
        return false;
    const uint8_t* p = c->data + 12 + c->regions * 4 + (unsigned)i * 32;
    s->uid = u32(p);
    s->name = str(c, u32(p + 4));
    s->url = str(c, u32(p + 8));
    s->py = str(c, u32(p + 12));
    s->heat = u32(p + 16);
    s->qt = u32(p + 20);
    s->region = str(c, u32(p + 24));
    s->region_id = p[28];
    s->codec = p[29];
    s->types = u16(p + 30);
    return s->name && s->url && s->py && s->region && s->region_id < c->regions && s->codec <= 2;
}
bool nrf_catalog_init(nrf_catalog_t* c, const void* data, size_t size) {
    if (!c)
        return false;
    memset(c, 0, sizeof(*c));
    if (!data || size < 12 || memcmp(data, "NFC1", 4))
        return false;
    const uint8_t* p = data;
    unsigned n = u16(p + 4), nr = u16(p + 6), st = u32(p + 8);
    if (!n || n > 4096 || !nr || nr > 64 || st != 12 + nr * 4 + n * 32 || st >= size)
        return false;
    c->data = data;
    c->size = size;
    c->count = n;
    c->regions = nr;
    c->strings = st;
    for (unsigned i = 0; i < nr; i++)
        if (!nrf_catalog_region(c, i))
            goto bad;
    for (unsigned i = 0; i < n; i++) {
        nrf_station_t s;
        if (!nrf_catalog_get(c, (int)i, &s))
            goto bad;
    }
    return true;
bad:
    memset(c, 0, sizeof(*c));
    return false;
}
const char* nrf_catalog_region(const nrf_catalog_t* c, unsigned i) {
    return c && c->data && i < c->regions ? str(c, u32(c->data + 12 + i * 4)) : NULL;
}
bool nrf_catalog_url(const nrf_station_t* s, char* url, size_t cap) {
    if (!s || !url || !cap)
        return false;
    int n = s->qt
                ? snprintf(url, cap, "http://lhttp.qtfm.cn/live/%lu/64k.mp3", (unsigned long)s->qt)
                : snprintf(url, cap, "%s", s->url);
    return n > 0 && (size_t)n < cap;
}
bool nrf_catalog_match(const nrf_station_t* s, const char* q) {
    if (!s || !q || !*q || strlen(q) > 63)
        return false;
    if (strstr(s->name, q) || strstr(s->region, q))
        return true;
    char lower[64];
    size_t n = strlen(q);
    for (size_t i = 0; i < n; i++)
        lower[i] = (char)tolower((unsigned char)q[i]);
    lower[n] = 0;
    if (strstr(s->py, lower))
        return true;
    for (unsigned i = 0; i < NRF_TYPES; i++)
        if ((s->types & (1u << i)) && strstr(nrf_catalog_type(i), q))
            return true;
    return false;
}
