/*
 * date: shows a time, formatted like GNU date does (the format letters, and the flags - _ 0 ^ # with a width).
 *
 * What it can read with -d: @SECONDS, YYYY-MM-DD, YYYY-MM-DD HH:MM[:SS], the same with T and a zone (Z or
 * +HH:MM), HH:MM[:SS], month names ("14 Oct 2026", "Oct 14, 2026"), now today yesterday tomorrow noon
 * midnight, weekday names, and relative parts like "+2 days", "3 weeks ago", "next month", "last year".
 * Setting the clock (-s) is not supported.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <windows.h>
#include "args.h"
#include "datetime.h"
#include "vpath.h"

#define weekdays datetime_weekdays
#define months datetime_months

#define VERSION "0.1"
#define PROGRAM "date"

static void print_usage(void) {
    puts("Usage: date [OPTION]... [+FORMAT]\n"
         "Display the current time in the given FORMAT, or the time given with -d.\n"
         "\n"
         "  -d, --date=STRING          display time described by STRING, not 'now'\n"
         "  -I[FMT], --iso-8601[=FMT]  output date/time in ISO 8601 format; FMT is 'date' (the\n"
         "                               default), 'hours', 'minutes', 'seconds' or 'ns'\n"
         "  -r, --reference=FILE       display the last modification time of FILE\n"
         "  -R, --rfc-email            output date and time in RFC 5322 format\n"
         "      --rfc-3339=FMT         output date and time in RFC 3339 format; FMT is 'date',\n"
         "                               'seconds' or 'ns'\n"
         "  -u, --utc, --universal     print Coordinated Universal Time (UTC)\n"
         "      --help                 display this help and exit\n"
         "      --version              output version information and exit\n"
         "\n"
         "FORMAT controls the output: %% a literal %, %a %A weekday, %b %B month, %c, %C century,\n"
         "%d %e day, %D, %F, %g %G %V ISO week, %H %I %k %l hour, %j day of the year, %m %M %n,\n"
         "%N nanoseconds, %p %P, %r %R, %s seconds since 1970, %S, %t, %T, %u %w, %U %W, %x %X,\n"
         "%y %Y, %z (also %:z and %::z), %Z. A flag after the %: - no padding, _ spaces, 0 zeros,\n"
         "^ upper case, # the other case; a number is the width.\n"
         "\n"
         "The time zone is the one of Windows (or TZ). Setting the clock is not supported.");
}

enum { OPT_DATE = 1, OPT_ISO, OPT_REFERENCE, OPT_RFC_EMAIL, OPT_RFC_3339, OPT_UTC, OPT_SET };

static const ArgsOption options[] = {
    { OPT_DATE,      'd', "date",       ARGS_REQUIRED },
    { OPT_ISO,       'I', "iso-8601",   ARGS_OPTIONAL },
    { OPT_REFERENCE, 'r', "reference",  ARGS_REQUIRED },
    { OPT_RFC_EMAIL, 'R', "rfc-email",  ARGS_NONE },
    { OPT_RFC_3339,  0,   "rfc-3339",   ARGS_REQUIRED },
    { OPT_UTC,       'u', "utc",        ARGS_NONE },
    { OPT_UTC,       0,   "universal",  ARGS_NONE },
    { OPT_SET,       's', "set",        ARGS_REQUIRED },
    { 0 }
};

static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

typedef struct {
    const char* date;
    const char* reference;
    const char* format;         /* from -I, -R, --rfc-3339; NULL for the default or the +FORMAT operand */
    int         utc;
} Options;

static int on_option(int id, const char* value, void* context) {
    Options* o = context;

    switch (id) {
        case OPT_DATE:
            o->date = value;
            break;
        case OPT_REFERENCE:
            o->reference = value;
            break;
        case OPT_UTC:
            o->utc = 1;
            break;
        case OPT_RFC_EMAIL:
            o->format = "%a, %d %b %Y %H:%M:%S %z";
            break;
        case OPT_ISO: {
            const char* kind = value != NULL ? value : "date";
            if (strcmp(kind, "date") == 0)
                o->format = "%Y-%m-%d";
            else if (strcmp(kind, "hours") == 0)
                o->format = "%Y-%m-%dT%H%:z";
            else if (strcmp(kind, "minutes") == 0)
                o->format = "%Y-%m-%dT%H:%M%:z";
            else if (strcmp(kind, "seconds") == 0)
                o->format = "%Y-%m-%dT%H:%M:%S%:z";
            else if (strcmp(kind, "ns") == 0)
                o->format = "%Y-%m-%dT%H:%M:%S,%N%:z";
            else {
                fprintf(stderr, PROGRAM ": invalid argument '%s' for '--iso-8601'\n", kind);
                return -1;
            }
            break;
        }
        case OPT_RFC_3339:
            if (strcmp(value, "date") == 0)
                o->format = "%Y-%m-%d";
            else if (strcmp(value, "seconds") == 0)
                o->format = "%Y-%m-%d %H:%M:%S%:z";
            else if (strcmp(value, "ns") == 0)
                o->format = "%Y-%m-%d %H:%M:%S.%N%:z";
            else {
                fprintf(stderr, PROGRAM ": invalid argument '%s' for '--rfc-3339'\n", value);
                return -1;
            }
            break;
        case OPT_SET:
            fprintf(stderr, PROGRAM ": setting the clock is not supported\n");
            return -1;
        default:
            break;
    }
    return 0;
}

/* ---- the time ------------------------------------------------------------------------------ */

typedef struct {
    struct tm tm;
    long      offset;           /* seconds east of UTC */
    char      zone[64];
} Broken;


/** The letters that start the words of a name: "Korea Standard Time" is "KST". */
static void initials(const char* name, char* out, size_t size) {
    size_t i, k = 0;

    for (i = 0; name[i] != '\0' && k + 1 < size; i++)
        if ((i == 0 || name[i - 1] == ' ') && isupper((unsigned char)name[i]))
            out[k++] = name[i];
    out[k] = '\0';
}

/**
 * The abbreviation of the time zone, as GNU date shows it. With TZ set it is what the C library has;
 * otherwise it comes from the English name of the Windows zone ("Korea Standard Time" is KST, in summer
 * "Pacific Daylight Time" is PDT).
 */
static void zone_abbreviation(int dst, char* out, size_t size) {
    DYNAMIC_TIME_ZONE_INFORMATION info;
    char key[160], name[160];
    const char* standard;

    if (getenv("TZ") != NULL) {
        size_t n = 0;
        char tz[64] = "";
        _tzset();
        _get_tzname(&n, tz, sizeof tz, dst ? 1 : 0);
        snprintf(out, size, "%s", n > 0 && tz[0] != '\0' ? tz : "UTC");
        return;
    }

    memset(&info, 0, sizeof info);
    GetDynamicTimeZoneInformation(&info);
    WideCharToMultiByte(CP_UTF8, 0, info.TimeZoneKeyName, -1, key, sizeof key, NULL, NULL);

    if (key[0] == '\0' || strcmp(key, "UTC") == 0 || strstr(key, "Coordinated Universal") != NULL) {
        snprintf(out, size, "UTC");
        return;
    }
    if (strcmp(key, "GMT Standard Time") == 0 || strcmp(key, "Greenwich Standard Time") == 0) {
        snprintf(out, size, "%s", dst ? "BST" : "GMT");
        return;
    }

    standard = strstr(key, "Standard");
    if (dst && standard != NULL) {
        // "Pacific Standard Time" in summer is the Daylight one
        snprintf(name, sizeof name, "%.*sDaylight%s", (int)(standard - key), key, standard + 8);
    } else {
        snprintf(name, sizeof name, "%s", key);
    }
    initials(name, out, size);
    if (out[0] == '\0')
        snprintf(out, size, "UTC");
}

static int break_down(__int64 seconds, int utc, Broken* b) {
    struct tm copy;

    memset(b, 0, sizeof *b);
    if (utc) {
        if (datetime_gmtime(seconds, &b->tm) != 0)
            return -1;
        b->offset = 0;
        snprintf(b->zone, sizeof b->zone, "UTC");
        return 0;
    }
    if (datetime_localtime(seconds, &b->tm) != 0)
        return -1;
    copy = b->tm;
    b->offset = (long)(datetime_gm_seconds(&copy) - seconds);
    zone_abbreviation(b->tm.tm_isdst > 0, b->zone, sizeof b->zone);
    return 0;
}

/* ---- the format ---------------------------------------------------------------------------- */

typedef struct {
    char   text[128];
    int    numeric;
    char   pad;                 /* ' ' or '0' for a number */
    int    width;
} Field;

static int iso_week(const struct tm* tm, int* year) {
    int wday = (tm->tm_wday + 6) % 7;                 /* Monday is 0 */
    int week = (tm->tm_yday - wday + 10) / 7;
    int y = tm->tm_year + 1900;

    if (week < 1) {
        int prev = y - 1;
        int leap = (prev % 4 == 0 && prev % 100 != 0) || prev % 400 == 0;
        int lastYday = tm->tm_yday + 365 + leap;       /* yday in the previous year */
        week = (lastYday - wday + 10) / 7;
        y = prev;
    } else if (week == 53) {
        int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        int daysInYear = 365 + leap;
        if (tm->tm_yday - wday + 3 >= daysInYear) {
            week = 1;
            y++;
        }
    }
    *year = y;
    return week;
}

static void number_field(Field* f, long long value, int width, char pad) {
    f->numeric = 1;
    f->width = width;
    f->pad = pad;
    snprintf(f->text, sizeof f->text, "%lld", value);
}

static void text_field(Field* f, const char* text) {
    f->numeric = 0;
    f->width = 0;
    f->pad = ' ';
    snprintf(f->text, sizeof f->text, "%s", text);
}

static void format_zone_offset(char* out, size_t size, long offset, int colons) {
    char sign = offset < 0 ? '-' : '+';
    long a = offset < 0 ? -offset : offset;
    int h = (int)(a / 3600), m = (int)(a % 3600 / 60), s = (int)(a % 60);

    if (colons == 0)
        snprintf(out, size, "%c%02d%02d", sign, h, m);
    else if (colons == 1)
        snprintf(out, size, "%c%02d:%02d", sign, h, m);
    else if (colons == 2)
        snprintf(out, size, "%c%02d:%02d:%02d", sign, h, m, s);
    else if (s != 0)
        snprintf(out, size, "%c%02d:%02d:%02d", sign, h, m, s);
    else if (m != 0)
        snprintf(out, size, "%c%02d:%02d", sign, h, m);
    else
        snprintf(out, size, "%c%02d", sign, h);
}

static void format_into(char* out, size_t size, const char* format, const Instant* t, const Broken* b);

/** A conversion that is a format of its own. */
static void composite(Field* f, const char* format, const Instant* t, const Broken* b) {
    format_into(f->text, sizeof f->text, format, t, b);
    f->numeric = 0;
}

/** Fills f for the conversion letter; returns 0 when the letter is not one. */
static int conversion(Field* f, int letter, int colons, int width, const Instant* t, const Broken* b) {
    const struct tm* tm = &b->tm;
    int year;

    switch (letter) {
        case 'a': text_field(f, weekdays[tm->tm_wday]); f->text[3] = '\0'; return 1;
        case 'A': text_field(f, weekdays[tm->tm_wday]); return 1;
        case 'b': case 'h': text_field(f, months[tm->tm_mon]); f->text[3] = '\0'; return 1;
        case 'B': text_field(f, months[tm->tm_mon]); return 1;
        case 'c': composite(f, "%a %b %e %H:%M:%S %-Y", t, b); return 1;
        case 'C': number_field(f, (tm->tm_year + 1900) / 100, 2, '0'); return 1;
        case 'd': number_field(f, tm->tm_mday, 2, '0'); return 1;
        case 'D': composite(f, "%m/%d/%y", t, b); return 1;
        case 'e': number_field(f, tm->tm_mday, 2, ' '); return 1;
        case 'F': composite(f, "%Y-%m-%d", t, b); return 1;
        case 'g': iso_week(tm, &year); number_field(f, year % 100, 2, '0'); return 1;
        case 'G': iso_week(tm, &year); number_field(f, year, 4, '0'); return 1;
        case 'H': number_field(f, tm->tm_hour, 2, '0'); return 1;
        case 'I': number_field(f, tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12, 2, '0'); return 1;
        case 'j': number_field(f, tm->tm_yday + 1, 3, '0'); return 1;
        case 'k': number_field(f, tm->tm_hour, 2, ' '); return 1;
        case 'l': number_field(f, tm->tm_hour % 12 == 0 ? 12 : tm->tm_hour % 12, 2, ' '); return 1;
        case 'm': number_field(f, tm->tm_mon + 1, 2, '0'); return 1;
        case 'M': number_field(f, tm->tm_min, 2, '0'); return 1;
        case 'n': text_field(f, "\n"); return 1;
        case 'N': {
            char digits[16];
            int w = width > 0 && width < 9 ? width : 9;
            snprintf(digits, sizeof digits, "%09ld", t->nanos);
            digits[w] = '\0';
            text_field(f, digits);
            return 1;
        }
        case 'p': text_field(f, tm->tm_hour < 12 ? "AM" : "PM"); return 1;
        case 'q': number_field(f, tm->tm_mon / 3 + 1, 1, '0'); return 1;
        case 'P': text_field(f, tm->tm_hour < 12 ? "am" : "pm"); return 1;
        case 'r': composite(f, "%I:%M:%S %p", t, b); return 1;
        case 'R': composite(f, "%H:%M", t, b); return 1;
        case 's': number_field(f, t->seconds, 1, '0'); return 1;
        case 'S': number_field(f, tm->tm_sec, 2, '0'); return 1;
        case 't': text_field(f, "\t"); return 1;
        case 'T': composite(f, "%H:%M:%S", t, b); return 1;
        case 'u': number_field(f, tm->tm_wday == 0 ? 7 : tm->tm_wday, 1, '0'); return 1;
        case 'U': number_field(f, (tm->tm_yday + 7 - tm->tm_wday) / 7, 2, '0'); return 1;
        case 'V': number_field(f, iso_week(tm, &year), 2, '0'); return 1;
        case 'w': number_field(f, tm->tm_wday, 1, '0'); return 1;
        case 'W': number_field(f, (tm->tm_yday + 7 - (tm->tm_wday + 6) % 7) / 7, 2, '0'); return 1;
        case 'x': composite(f, "%m/%d/%y", t, b); return 1;
        case 'X': composite(f, "%H:%M:%S", t, b); return 1;
        case 'y': number_field(f, (tm->tm_year + 1900) % 100, 2, '0'); return 1;
        case 'Y': number_field(f, tm->tm_year + 1900, 4, '0'); return 1;
        case 'z': format_zone_offset(f->text, sizeof f->text, b->offset, colons); f->numeric = 0; return 1;
        case 'Z': text_field(f, b->zone); return 1;
        default: return 0;
    }
}

static void upper(char* s) {
    for (; *s != '\0'; s++)
        *s = (char)toupper((unsigned char)*s);
}

static void lower(char* s) {
    for (; *s != '\0'; s++)
        *s = (char)tolower((unsigned char)*s);
}

static void format_into(char* out, size_t size, const char* format, const Instant* t, const Broken* b) {
    size_t o = 0;
    const char* p;

#define PUT(c) do { if (o + 1 < size) out[o++] = (char)(c); } while (0)

    for (p = format; *p != '\0'; p++) {
        const char* start = p;
        int noPad = 0, spacePad = 0, zeroPad = 0, up = 0, swap = 0, width = -1, colons = 0;
        Field f;
        size_t i, length;

        if (*p != '%') {
            PUT(*p);
            continue;
        }
        p++;
        for (;; p++) {
            if (*p == '-') noPad = 1;
            else if (*p == '_') spacePad = 1;
            else if (*p == '0') zeroPad = 1;
            else if (*p == '^') up = 1;
            else if (*p == '#') swap = 1;
            else break;
        }
        if (isdigit((unsigned char)*p)) {
            width = 0;
            while (isdigit((unsigned char)*p))
                width = width * 10 + (*p++ - '0');
        }
        while (*p == ':') {
            colons++;
            p++;
        }
        if (*p == '\0') {
            for (; *start != '\0'; start++)
                PUT(*start);
            break;
        }
        if (*p == '%') {
            PUT('%');
            continue;
        }
        memset(&f, 0, sizeof f);
        if (!conversion(&f, (unsigned char)*p, colons, width, t, b)) {
            // not a conversion: the text as it was
            for (; start <= p; start++)
                PUT(*start);
            continue;
        }

        if (up || (swap && *p != 'p' && *p != 'Z'))
            upper(f.text);
        else if (swap)
            lower(f.text);

        length = strlen(f.text);
        if (*p == 'N') {
            width = -1;                 /* the width of %N counted digits */
        }
        if (f.numeric) {
            char pad = f.pad;
            int w = f.width;
            if (width >= 0) {
                w = width;
                if (width > f.width && !spacePad && !zeroPad && pad == ' ')
                    pad = ' ';
            }
            if (spacePad)
                pad = ' ';
            if (zeroPad)
                pad = '0';
            if (noPad)
                w = 0;
            if (f.text[0] == '-' && pad == '0' && length < (size_t)w) {
                PUT('-');
                for (i = length; i < (size_t)w; i++)
                    PUT('0');
                for (i = 1; i < length; i++)
                    PUT(f.text[i]);
                continue;
            }
            for (i = length; i < (size_t)w; i++)
                PUT(pad);
        } else if (width > 0 && !noPad && length < (size_t)width) {
            char pad = zeroPad ? '0' : ' ';
            for (i = length; i < (size_t)width; i++)
                PUT(pad);
        }
        for (i = 0; i < length; i++)
            PUT(f.text[i]);
    }
    out[o < size ? o : size - 1] = '\0';
#undef PUT
}

static int file_instant(const char* path, Instant* out) {
    char native[VPATH_MAX];
    WIN32_FILE_ATTRIBUTE_DATA data;
    unsigned long long t;

    if (vpath_to_native(path, native, sizeof native) != 0 ||
        !GetFileAttributesExA(native, GetFileExInfoStandard, &data))
        return -1;
    t = ((unsigned long long)data.ftLastWriteTime.dwHighDateTime << 32) | data.ftLastWriteTime.dwLowDateTime;
    out->seconds = (__int64)(t / 10000000ULL) - 11644473600LL;
    out->nanos = (long)(t % 10000000ULL) * 100;
    return 0;
}

int main(int argc, char* argv[]) {
    Options o;
    const char** operands;
    int count, result;
    Instant instant;
    Broken broken;
    const char* format;
    char text[1024];

    memset(&o, 0, sizeof o);
    result = args_parse(&spec, argc, argv, on_option, &o, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);

    if (count > 1 || (count == 1 && operands[0][0] != '+')) {
        fprintf(stderr, PROGRAM ": invalid date '%s'\n", operands[count > 1 && operands[0][0] == '+' ? 1 : 0]);
        return 1;
    }
    if (count == 1) {
        if (o.format != NULL) {
            fprintf(stderr, PROGRAM ": multiple output formats specified\n");
            return 1;
        }
        format = operands[0] + 1;
    } else {
        format = o.format != NULL ? o.format : "%a %b %e %H:%M:%S %Z %Y";
    }

    if (o.date != NULL && o.reference != NULL) {
        fprintf(stderr, PROGRAM ": the options to specify dates for printing are mutually exclusive\n");
        return 1;
    }

    instant = datetime_now();
    if (o.reference != NULL) {
        if (file_instant(o.reference, &instant) != 0) {
            fprintf(stderr, PROGRAM ": %s: No such file or directory\n", o.reference);
            return 1;
        }
    } else if (o.date != NULL) {
        if (datetime_parse(o.date, &instant, o.utc, &instant) != 0) {
            fprintf(stderr, PROGRAM ": invalid date '%s'\n", o.date);
            return 1;
        }
    }

    if (break_down(instant.seconds, o.utc, &broken) != 0) {
        fprintf(stderr, PROGRAM ": time out of range\n");
        return 1;
    }
    format_into(text, sizeof text, format, &instant, &broken);

    _setmode(_fileno(stdout), _O_BINARY);
    fputs(text, stdout);
    fputc('\n', stdout);
    return fflush(stdout) != 0 ? 1 : 0;
}
