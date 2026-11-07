#ifndef BORDIFY_RTC_H
#define BORDIFY_RTC_H

#include "types.h"

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} rtc_time_t;

void rtc_read(rtc_time_t *out);
void rtc_format_time(char *out, size_t size);
void rtc_format_date(char *out, size_t size);

#endif
