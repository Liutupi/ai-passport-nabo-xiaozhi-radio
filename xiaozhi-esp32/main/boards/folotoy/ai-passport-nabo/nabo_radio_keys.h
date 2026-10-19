#pragma once
#include <stdbool.h>
#include <stdint.h>
enum { NRF_UP, NRF_OK, NRF_DOWN };
enum { NRF_CLICK, NRF_DOUBLE, NRF_LONG, NRF_REPEAT };
typedef void (*nrf_key_fn)(void*, int key, int event);
typedef struct {
    uint32_t since[3], pending[3], repeat_at;
    bool down[3], held[3];
    unsigned repeats;
    nrf_key_fn emit;
    void* ctx;
} nrf_keys_t;
void nrf_keys_init(nrf_keys_t* k, nrf_key_fn emit, void* ctx);
void nrf_keys_tick(nrf_keys_t* k, uint32_t now);
void nrf_keys_edge(nrf_keys_t* k, int key, bool down, uint32_t now);
