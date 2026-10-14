#include "datetime.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* const datetime_weekdays[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
const char* const datetime_months[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
                                              "September", "October", "November", "December" };

#define weekdays datetime_weekdays
#define months datetime_months

Instant datetime_now(void) {
    FILETIME ft;
    unsigned long long t;
    Instant i;

    GetSystemTimePreciseAsFileTime(&ft);
    t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    i.seconds = (__int64)(t / 10000000ULL) - 11644473600LL;
    i.nanos = (long)(t % 10000000ULL) * 100;
    return i;
}

/* ---- calendar arithmetic (the C library only knows 1970 to 3000) ------------------------------ */

static __int64 floor_div(__int64 a, __int64 b) {
    __int64 q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

/** Days since 1970-01-01 of a date of the proleptic Gregorian calendar (month 1..12). */
static __int64 days_from_civil(__int64 y, int m, int d) {
    __int64 era, yoe, doy, doe;

    y -= m <= 2;
    era = floor_div(y, 400);
    yoe = y - era * 400;
    doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

static int is_leap(__int64 y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

/** Breaks seconds since 1970 (UTC) down; any year. */
static int gm_tm(__int64 seconds, struct tm* tm) {
    __int64 days = floor_div(seconds, 86400), rest = seconds - days * 86400;
    __int64 z = days + 719468, era = floor_div(z, 146097), doe = z - era * 146097;
    __int64 yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400;
    __int64 doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);

    y += m <= 2;
    memset(tm, 0, sizeof *tm);
    tm->tm_year = (int)(y - 1900);
    tm->tm_mon = m - 1;
    tm->tm_mday = d;
    tm->tm_hour = (int)(rest / 3600);
    tm->tm_min = (int)(rest % 3600 / 60);
    tm->tm_sec = (int)(rest % 60);
    tm->tm_wday = (int)(((days % 7) + 11) % 7);       /* 1970-01-01 was a Thursday */
    tm->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    tm->tm_isdst = 0;
    (void)is_leap;
    return 0;
}

/** Seconds since 1970 (UTC) of a broken down time, whose fields may be out of range. */
static __int64 gm_seconds(const struct tm* tm) {
    __int64 year = (__int64)tm->tm_year + 1900 + floor_div(tm->tm_mon, 12);
    int month = (int)(tm->tm_mon - floor_div(tm->tm_mon, 12) * 12) + 1;

    return (days_from_civil(year, month, 1) + (tm->tm_mday - 1)) * 86400 + (__int64)tm->tm_hour * 3600 +
           (__int64)tm->tm_min * 60 + tm->tm_sec;
}

/** The offset of the zone outside the years the C library knows: the standard one (no summer time). */
static long standard_offset(void) {
    TIME_ZONE_INFORMATION info;
    memset(&info, 0, sizeof info);
    GetTimeZoneInformation(&info);
    return -(info.Bias + info.StandardBias) * 60L;
}

#define LIBRARY_MAX 32535215999LL

static int local_tm(__int64 seconds, struct tm* tm) {
    if (seconds >= 0 && seconds <= LIBRARY_MAX)
        return _localtime64_s(tm, &seconds);
    return gm_tm(seconds + standard_offset(), tm);
}

/** The instant of a local broken down time (fields may be out of range). */
static __int64 local_seconds(struct tm* tm) {
    struct tm copy = *tm;
    __int64 guess = gm_seconds(tm) - standard_offset();

    if (guess >= 0 && guess <= LIBRARY_MAX - 86400 * 2) {
        __int64 t = _mktime64(&copy);
        if (t != -1)
            return t;
    }
    return guess;
}

/* ---- reading a date ------------------------------------------------------------------------ */

typedef struct {
    int       year, month, day;         /* month 1..12; 0 when not given */
    int       hour, minute, second;
    int       haveDate, haveTime;
    int       weekday;                  /* -1, or the wanted day */
    int       weekdayOrdinal;           /* 0 this one or the next, 1 strictly after today, -1 strictly before */
    int       haveZone;
    long      zoneOffset;
    long long relSeconds, relDays, relMonths;
    long long epoch;
    long      nanos;            /* of an @SECONDS.FRACTION */
    int       haveEpoch;
} Parsed;

static int lower_equals(const char* a, const char* b) {
    return _stricmp(a, b) == 0;
}

static int month_by_name(const char* s) {
    int i;
    size_t n = strlen(s);

    if (n < 3)
        return 0;
    for (i = 0; i < 12; i++)
        if (_strnicmp(s, months[i], n) == 0 && (n == 3 || n == strlen(months[i])))
            return i + 1;
    return 0;
}

static int weekday_by_name(const char* s) {
    int i;
    size_t n = strlen(s);

    if (n < 3)
        return -1;
    for (i = 0; i < 7; i++)
        if (_strnicmp(s, weekdays[i], n) == 0 && (n == 3 || n == strlen(weekdays[i])))
            return i;
    return -1;
}

/** A unit as a number of seconds, days (-1 in days: 7 per week) or months; returns the kind. */
static int unit_by_name(const char* s, long long* amount) {
    char w[16];
    size_t n = strlen(s);

    if (n == 0 || n >= sizeof w)
        return 0;
    snprintf(w, sizeof w, "%s", s);
    if (n > 1 && w[n - 1] == 's')
        w[n - 1] = '\0';
    if (lower_equals(w, "sec") || lower_equals(w, "second")) { *amount = 1; return 's'; }
    if (lower_equals(w, "min") || lower_equals(w, "minute")) { *amount = 60; return 's'; }
    if (lower_equals(w, "hour")) { *amount = 3600; return 's'; }
    if (lower_equals(w, "day")) { *amount = 1; return 'd'; }
    if (lower_equals(w, "week")) { *amount = 7; return 'd'; }
    if (lower_equals(w, "fortnight")) { *amount = 14; return 'd'; }
    if (lower_equals(w, "month")) { *amount = 1; return 'm'; }
    if (lower_equals(w, "year")) { *amount = 12; return 'm'; }
    return 0;
}

static void add_relative(Parsed* r, int kind, long long amount, long long count) {
    if (kind == 's')
        r->relSeconds += amount * count;
    else if (kind == 'd')
        r->relDays += amount * count;
    else
        r->relMonths += amount * count;
}

static int all_digits(const char* s) {
    if (*s == '\0')
        return 0;
    for (; *s != '\0'; s++)
        if (!isdigit((unsigned char)*s))
            return 0;
    return 1;
}

/** HH:MM[:SS][.frac] with an optional zone after it (Z, +HH:MM, +HHMM). */
static int parse_time_token(const char* s, Parsed* r) {
    int h, m, sec = 0, used = 0;
    char zone[16] = "";

    if (sscanf(s, "%d:%d%n", &h, &m, &used) != 2)
        return 0;
    s += used;
    if (*s == ':') {
        int n2 = 0;
        if (sscanf(s + 1, "%d%n", &sec, &n2) != 1)
            return 0;
        s += 1 + n2;
        if (*s == '.' || *s == ',') {
            s++;
            while (isdigit((unsigned char)*s))
                s++;
        }
    }
    if (h < 0 || h > 24 || m < 0 || m > 59 || sec < 0 || sec > 60)
        return 0;
    if (*s != '\0') {
        snprintf(zone, sizeof zone, "%s", s);
        if (strcmp(zone, "Z") == 0 || lower_equals(zone, "utc") || lower_equals(zone, "gmt")) {
            r->haveZone = 1;
            r->zoneOffset = 0;
        } else if ((zone[0] == '+' || zone[0] == '-') && strlen(zone) >= 3) {
            int zh = 0, zm = 0;
            const char* q = zone + 1;
            if (strchr(q, ':') != NULL) {
                if (sscanf(q, "%d:%d", &zh, &zm) < 1)
                    return 0;
            } else if (strlen(q) <= 2) {
                zh = atoi(q);
            } else {
                int v = atoi(q);
                zh = v / 100;
                zm = v % 100;
            }
            r->haveZone = 1;
            r->zoneOffset = (zone[0] == '-' ? -1 : 1) * (zh * 3600L + zm * 60L);
        } else {
            return 0;
        }
    }
    r->hour = h;
    r->minute = m;
    r->second = sec;
    r->haveTime = 1;
    return 1;
}

/** YYYY-MM-DD, optionally with T and a time. */
static int parse_date_token(const char* s, Parsed* r) {
    int y, m, d, used = 0;

    if (sscanf(s, "%d-%d-%d%n", &y, &m, &d, &used) != 3)
        return 0;
    if (m < 1 || m > 12 || d < 1 || d > 31)
        return 0;
    r->year = y;
    r->month = m;
    r->day = d;
    r->haveDate = 1;
    s += used;
    if (*s == 'T' || *s == 't')
        return parse_time_token(s + 1, r);
    if ((*s == 'Z' || *s == 'z') && s[1] == '\0') {
        r->haveZone = 1;                // 2003-03-04Z: midnight in UTC
        r->zoneOffset = 0;
        return 1;
    }
    return *s == '\0';
}

static int parse_date_string(const char* text, Parsed* r, const struct tm* nowTm) {
    char copy[256];
    char* tokens[64];
    int count = 0, i;
    char* p;

    memset(r, 0, sizeof *r);
    r->weekday = -1;
    if (strlen(text) >= sizeof copy)
        return -1;
    snprintf(copy, sizeof copy, "%s", text);

    for (p = copy; *p != '\0';) {
        while (*p == ' ' || *p == '\t' || *p == ',')
            p++;
        if (*p == '\0')
            break;
        if (count == 64)
            return -1;
        tokens[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != ',')
            p++;
        if (*p != '\0')
            *p++ = '\0';
    }
    if (count == 0) {
        // an empty string is midnight today
        r->hour = r->minute = r->second = 0;
        r->haveTime = 1;
        return 0;
    }

    if (tokens[0][0] == '@' && count == 1) {
        char* end;
        r->epoch = _strtoi64(tokens[0] + 1, &end, 10);
        if (end == tokens[0] + 1)
            return -1;
        if (*end == '.' || *end == ',') {
            long scale = 100000000;
            end++;
            while (isdigit((unsigned char)*end)) {
                r->nanos += (*end - '0') * scale;
                scale /= 10;
                end++;
            }
        }
        if (*end != '\0')
            return -1;
        r->haveEpoch = 1;
        return 0;
    }

    for (i = 0; i < count; i++) {
        const char* t = tokens[i];
        long long amount;
        int kind, month, wd;

        if (lower_equals(t, "now") || lower_equals(t, "today")) {
            continue;
        } else if (lower_equals(t, "yesterday")) {
            r->relDays -= 1;
        } else if (lower_equals(t, "tomorrow")) {
            r->relDays += 1;
        } else if (lower_equals(t, "noon")) {
            r->hour = 12; r->minute = r->second = 0; r->haveTime = 1;
        } else if (lower_equals(t, "midnight")) {
            r->hour = r->minute = r->second = 0; r->haveTime = 1;
        } else if (lower_equals(t, "utc") || lower_equals(t, "gmt") || strcmp(t, "Z") == 0) {
            r->haveZone = 1;
            r->zoneOffset = 0;
        } else if (lower_equals(t, "ago")) {
            r->relSeconds = -r->relSeconds;
            r->relDays = -r->relDays;
            r->relMonths = -r->relMonths;
        } else if (strchr(t, ':') != NULL && parse_time_token(t, r)) {
            continue;
        } else if (strchr(t, '-') != NULL && isdigit((unsigned char)t[0]) && parse_date_token(t, r)) {
            continue;
        } else if ((lower_equals(t, "am") || lower_equals(t, "pm")) && r->haveTime) {
            if (lower_equals(t, "pm") && r->hour < 12)
                r->hour += 12;
            else if (lower_equals(t, "am") && r->hour == 12)
                r->hour = 0;
        } else if (lower_equals(t, "next") || lower_equals(t, "last") || lower_equals(t, "this")) {
            long long sign = lower_equals(t, "next") ? 1 : (lower_equals(t, "last") ? -1 : 0);
            if (i + 1 >= count)
                return -1;
            i++;
            kind = unit_by_name(tokens[i], &amount);
            if (kind == 0) {
                wd = weekday_by_name(tokens[i]);
                if (wd < 0)
                    return -1;
                r->weekday = wd;
                r->weekdayOrdinal = (int)sign;
            } else {
                add_relative(r, kind, amount, sign);
            }
        } else if ((t[0] == '+' || t[0] == '-' || isdigit((unsigned char)t[0])) && i + 1 < count &&
                   unit_by_name(tokens[i + 1], &amount) != 0 && all_digits(t[0] == '+' || t[0] == '-' ? t + 1 : t)) {
            long long n = _strtoi64(t, NULL, 10);
            kind = unit_by_name(tokens[i + 1], &amount);
            add_relative(r, kind, amount, n);
            i++;
        } else if ((month = month_by_name(t)) != 0) {
            r->month = month;
            r->haveDate = 1;
            if (r->day == 0)
                r->day = 1;
            // the day and the year may follow
            if (i + 1 < count && all_digits(tokens[i + 1]) && strlen(tokens[i + 1]) <= 2) {
                r->day = atoi(tokens[++i]);
            }
            if (i + 1 < count && all_digits(tokens[i + 1]) && strlen(tokens[i + 1]) == 4)
                r->year = atoi(tokens[++i]);
        } else if ((wd = weekday_by_name(t)) >= 0) {
            r->weekday = wd;
        } else if (all_digits(t) && strlen(t) <= 2 && r->month != 0 && r->haveDate) {
            r->day = atoi(t);
        } else if (all_digits(t) && strlen(t) == 4 && r->haveDate) {
            r->year = atoi(t);
        } else if (all_digits(t) && strlen(t) <= 2 && i + 1 < count && month_by_name(tokens[i + 1]) != 0) {
            // "14 Oct 2026"
            r->day = atoi(t);
            r->month = month_by_name(tokens[++i]);
            r->haveDate = 1;
            if (i + 1 < count && all_digits(tokens[i + 1]) && strlen(tokens[i + 1]) == 4)
                r->year = atoi(tokens[++i]);
        } else {
            return -1;
        }
    }
    (void)nowTm;
    return 0;
}

/** The instant a parsed date stands for, relative to now. Returns 0, or -1 when it does not exist. */
static int resolve_date(const Parsed* r, const Instant* now, int utc, Instant* out) {
    struct tm tm;
    __int64 seconds;

    if (r->haveEpoch) {
        out->seconds = r->epoch;
        out->nanos = r->nanos;
        return 0;
    }

    // the date of "now" where the date is told: in the zone that was given, in UTC, or here
    {
        __int64 reference = now->seconds + (r->haveZone ? r->zoneOffset : 0);
        int broken = (utc || r->haveZone) ? gm_tm(reference, &tm) : local_tm(reference, &tm);
        if (broken != 0)
            return -1;
    }

    if (r->haveDate) {
        if (r->year != 0)
            tm.tm_year = r->year - 1900;
        if (r->month != 0)
            tm.tm_mon = r->month - 1;
        if (r->day != 0)
            tm.tm_mday = r->day;
    }
    if (r->haveTime) {
        tm.tm_hour = r->hour;
        tm.tm_min = r->minute;
        tm.tm_sec = r->second;
    } else if (r->haveDate) {
        tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
    }
    tm.tm_mon += (int)r->relMonths;
    tm.tm_mday += (int)r->relDays;
    tm.tm_isdst = -1;

    if (r->weekday >= 0 && !r->haveDate) {
        // the first such day from today on
        struct tm probe = tm;
        __int64 t0 = r->haveZone || utc ? gm_seconds(&probe) : local_seconds(&probe);
        if (r->haveZone || utc ? gm_tm(t0, &probe) != 0 : local_tm(t0, &probe) != 0)
            return -1;
        int delta = (r->weekday - probe.tm_wday + 7) % 7;
        if (r->weekdayOrdinal > 0 && delta == 0)
            delta = 7;
        else if (r->weekdayOrdinal < 0)
            delta = delta == 0 ? -7 : delta - 7;
        tm.tm_mday += delta;
    }

    if (r->haveZone)
        seconds = gm_seconds(&tm) - r->zoneOffset;
    else if (utc)
        seconds = gm_seconds(&tm);
    else
        seconds = local_seconds(&tm);
    out->seconds = seconds + r->relSeconds;
    out->nanos = (r->haveTime || r->haveDate) ? 0 : now->nanos;
    return 0;
}

int datetime_gmtime(__int64 seconds, struct tm* tm) {
    return gm_tm(seconds, tm);
}

__int64 datetime_gm_seconds(const struct tm* tm) {
    return gm_seconds(tm);
}

int datetime_localtime(__int64 seconds, struct tm* tm) {
    return local_tm(seconds, tm);
}

__int64 datetime_local_seconds(struct tm* tm) {
    return local_seconds(tm);
}

int datetime_parse(const char* text, const Instant* now, int utc, Instant* out) {
    Parsed parsed;
    struct tm nowTm;

    local_tm(now->seconds, &nowTm);
    if (parse_date_string(text, &parsed, &nowTm) != 0)
        return -1;
    return resolve_date(&parsed, now, utc, out);
}

Instant datetime_from_filetime(FILETIME ft) {
    unsigned long long t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    Instant i;

    i.seconds = (__int64)(t / 10000000ULL) - 11644473600LL;
    i.nanos = (long)(t % 10000000ULL) * 100;
    return i;
}

FILETIME datetime_to_filetime(Instant instant) {
    unsigned long long t = (unsigned long long)(instant.seconds + 11644473600LL) * 10000000ULL +
                           (unsigned long long)(instant.nanos / 100);
    FILETIME ft;

    ft.dwLowDateTime = (DWORD)(t & 0xFFFFFFFFULL);
    ft.dwHighDateTime = (DWORD)(t >> 32);
    return ft;
}
