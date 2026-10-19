#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define NRF_TYPES 11
typedef struct {
    const uint8_t* data;
    size_t size;
    unsigned count, regions, strings;
} nrf_catalog_t;
typedef struct {
    uint32_t uid, heat, qt;
    const char *name, *url, *py, *region;
    unsigned region_id, codec, types; /* codec 0 MP3, 1 AAC, 2 HLS */
} nrf_station_t;
bool nrf_catalog_init(nrf_catalog_t* c, const void* data, size_t size);
bool nrf_catalog_get(const nrf_catalog_t* c, int i, nrf_station_t* station);
const char* nrf_catalog_region(const nrf_catalog_t* c, unsigned region);
const char* nrf_catalog_type(unsigned type);
bool nrf_catalog_url(const nrf_station_t* s, char* url, size_t capacity);
bool nrf_catalog_match(const nrf_station_t* s, const char* query);
