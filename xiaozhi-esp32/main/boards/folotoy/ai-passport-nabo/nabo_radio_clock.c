#include "nabo_radio_clock.h"
#include <stdio.h>
#include <string.h>
#ifndef NRF_CLOCK_HOST
#include "freertos/FreeRTOS.h"
static portMUX_TYPE clock_lock = portMUX_INITIALIZER_UNLOCKED;
#define CLOCK_LOCK() portENTER_CRITICAL(&clock_lock)
#define CLOCK_UNLOCK() portEXIT_CRITICAL(&clock_lock)
#else
#define CLOCK_LOCK() ((void)0)
#define CLOCK_UNLOCK() ((void)0)
#endif

static int64_t utc_seconds, anchor_us;
static unsigned digits(const char* p, unsigned n) {
    unsigned value = 0;
    for (unsigned i = 0; i < n; i++) {
        if (p[i] < '0' || p[i] > '9')
            return 10000;
        value = value * 10 + (unsigned)(p[i] - '0');
    }
    return value;
}
static bool leap(unsigned y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
bool nrf_clock_http_date(const char* date, int64_t now_us) {
    // IMF-fixdate, e.g. Mon, 19 Oct 2026 10:25:00 GMT. No locale or heap use.
    if (!date || strlen(date) != 29 || now_us < 0 || date[3] != ',' || date[4] != ' ' ||
        date[7] != ' ' || date[11] != ' ' || date[16] != ' ' || date[19] != ':' ||
        date[22] != ':' || strcmp(date + 25, " GMT"))
        return false;
    static const char* const months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    static const unsigned lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned month = 0;
    while (month < 12 && memcmp(date + 8, months[month], 3))
        month++;
    unsigned day = digits(date + 5, 2), year = digits(date + 12, 4);
    unsigned hour = digits(date + 17, 2), minute = digits(date + 20, 2),
             second = digits(date + 23, 2);
    if (month == 12 || year < 2024 || year > 2100 || !day ||
        day > lengths[month] + (month == 1 && leap(year)) || hour > 23 || minute > 59 ||
        second > 59)
        return false;
    int64_t days = day - 1;
    for (unsigned y = 1970; y < year; y++)
        days += 365 + leap(y);
    for (unsigned m = 0; m < month; m++)
        days += lengths[m] + (m == 1 && leap(year));
    int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    CLOCK_LOCK();
    utc_seconds = seconds;
    anchor_us = now_us;
    CLOCK_UNLOCK();
    return true;
}
bool nrf_clock_text(char* out, size_t capacity, int64_t now_us) {
    if (!out || capacity < 6)
        return false;
    CLOCK_LOCK();
    int64_t utc = utc_seconds, anchor = anchor_us;
    CLOCK_UNLOCK();
    if (!utc || now_us < anchor) {
        memcpy(out, "--:--", 6);
        return false;
    }
    // Asia/Shanghai: fixed UTC+8. Do not apply the system TZ a second time.
    int64_t seconds = utc + (now_us - anchor) / 1000000 + 8 * 3600;
    unsigned minute = (unsigned)((seconds / 60) % 1440);
    snprintf(out, capacity, "%02u:%02u", minute / 60, minute % 60);
    return true;
}
