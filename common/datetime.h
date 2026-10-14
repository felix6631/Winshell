#ifndef DATETIME_H
#define DATETIME_H

#include <time.h>
#include <windows.h>

/*
 * Times and dates, as date and touch need them.
 *
 * The C library knows the years 1970 to 3000 only, so the calendar is done here for any year (UTC, and the
 * local zone with its standard offset outside of what the library knows). datetime_parse reads what GNU date
 * reads with -d: @SECONDS, YYYY-MM-DD with a time and a zone, month names, now/yesterday/tomorrow, weekdays and
 * relative parts like "+2 days" or "3 weeks ago".
 */

typedef struct {
    __int64 seconds;            /* since 1970-01-01 00:00:00 UTC */
    long    nanos;
} Instant;

/** The English names of the days (from Sunday) and of the months. */
extern const char* const datetime_weekdays[7];
extern const char* const datetime_months[12];

/** The current time. */
Instant datetime_now(void);

/** Breaks seconds since 1970 (UTC) down. Returns 0. */
int datetime_gmtime(__int64 seconds, struct tm* tm);

/** Seconds since 1970 (UTC) of a broken down UTC time, whose fields may be out of range. */
__int64 datetime_gm_seconds(const struct tm* tm);

/** The local time of an instant. Returns 0, or -1 when it cannot be told. */
int datetime_localtime(__int64 seconds, struct tm* tm);

/** The instant of a local broken down time (fields may be out of range). */
__int64 datetime_local_seconds(struct tm* tm);

/**
 * Reads a date string relative to now. With utc a date without a zone is a UTC one, otherwise a local one.
 * Returns 0, or -1 when the text is not a date.
 */
int datetime_parse(const char* text, const Instant* now, int utc, Instant* out);

Instant datetime_from_filetime(FILETIME ft);
FILETIME datetime_to_filetime(Instant instant);

#endif //DATETIME_H
