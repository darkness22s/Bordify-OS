#include "rtc.h"
#include "io.h"
#include "printf.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    return inb(0x71);
}

static bool update_in_progress(void) {
    return (cmos_read(0x0A) & 0x80) != 0;
}

static uint8_t bcd_to_bin(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

void rtc_read(rtc_time_t *out) {
    while (update_in_progress()) {
    }
    uint8_t second = cmos_read(0x00);
    uint8_t minute = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day = cmos_read(0x07);
    uint8_t month = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t weekday = cmos_read(0x06);
    uint8_t status_b = cmos_read(0x0B);
    bool pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if (!(status_b & 0x04)) {
        second = bcd_to_bin(second);
        minute = bcd_to_bin(minute);
        hour = bcd_to_bin(hour);
        day = bcd_to_bin(day);
        month = bcd_to_bin(month);
        year = bcd_to_bin(year);
    }
    if (!(status_b & 0x02)) {
        if (hour == 12) {
            hour = 0;
        }
        if (pm) {
            hour = (uint8_t)(hour + 12);
        }
    }

    out->second = second;
    out->minute = minute;
    out->hour = hour;
    out->day = day;
    out->month = month;
    out->year = (uint16_t)(2000 + year);
    out->weekday = weekday;
}

static int32_t rtc_days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    int era = (year >= 0 ? year : year - 399) / 400;
    unsigned yoe = (unsigned)(year - era * 400);
    int mp = (int)month + ((int)month > 2 ? -3 : 9);
    unsigned doy = (unsigned)((153 * mp + 2) / 5) + day - 1u;
    unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + (int32_t)doe - 719468;
}

uint32_t rtc_unix_time(void) {
    rtc_time_t now;
    rtc_read(&now);
    if (now.year < 1970 || now.year > 2105 ||
        now.month < 1 || now.month > 12 || now.day < 1 || now.day > 31 ||
        now.hour > 23 || now.minute > 59 || now.second > 59) {
        return 0;
    }
    int32_t days = rtc_days_from_civil((int)now.year, now.month, now.day);
    return (uint32_t)days * 86400u + (uint32_t)now.hour * 3600u +
           (uint32_t)now.minute * 60u + (uint32_t)now.second;
}

void rtc_format_time(char *out, size_t size) {
    rtc_time_t now;
    rtc_read(&now);
    snprintf(out, size, "%02u:%02u:%02u", now.hour, now.minute, now.second);
}

void rtc_format_date(char *out, size_t size) {
    static const char *months[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    rtc_time_t now;
    rtc_read(&now);
    const char *month = (now.month >= 1 && now.month <= 12) ? months[now.month - 1] : "?";
    snprintf(out, size, "%s %u, %u", month, now.day, now.year);
}
