#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Radio HTTP Date is UTC. Keep a monotonic anchor independent of XiaoZhi's
 * legacy system clock, which may already contain a timezone offset. */
bool nrf_clock_http_date(const char* date, int64_t now_us);
bool nrf_clock_text(char* out, size_t capacity, int64_t now_us);
#ifdef __cplusplus
}
#endif
