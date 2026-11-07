#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tabstops.h"

#define TAB_LIMIT 0x3fffffffffffffffLL

void tabstops_init(TabStops* t) {
    memset(t, 0, sizeof *t);
    t->tabSize = 8;
}

void tabstops_free(TabStops* t) {
    free(t->stops);
    memset(t, 0, sizeof *t);
}

static int add_stop(const char* program, TabStops* t, long long value, const char* whole) {
    long long* grown;

    if (value == 0) {
        fprintf(stderr, "%s: tab size cannot be 0\n", program);
        return -1;
    }
    if (t->count > 0 && t->stops[t->count - 1] >= value) {
        fprintf(stderr, "%s: tab sizes must be ascending\n", program);
        return -1;
    }
    (void)whole;
    grown = realloc(t->stops, (t->count + 1) * sizeof *grown);
    if (grown == NULL) {
        fprintf(stderr, "%s: memory exhausted\n", program);
        return -1;
    }
    t->stops = grown;
    t->stops[t->count++] = value;
    return 0;
}

int tabstops_add(const char* program, const char* text, TabStops* t) {
    const char* p = text;
    int lastWasSpecial = 0;

    while (*p != '\0') {
        char kind = 0;
        long long value = 0;
        int digits = 0;

        while (*p == ',' || *p == ' ' || *p == '\t')
            p++;
        if (*p == '\0')
            break;
        if (*p == '/' || *p == '+') {
            kind = *p++;
        }
        while (isdigit((unsigned char)*p)) {
            if (value > (TAB_LIMIT - (*p - '0')) / 10) {
                fprintf(stderr, "%s: tab stop is too large '%s'\n", program, text);
                return -1;
            }
            value = value * 10 + (*p++ - '0');
            digits++;
        }
        if (digits == 0 && kind != 0 && (*p == '\0' || *p == ',' || *p == ' ' || *p == '\t'))
            continue;                       // a / or + with no number is left out
        if (digits == 0 || (*p != '\0' && *p != ',' && *p != ' ' && *p != '\t')) {
            fprintf(stderr, "%s: tab size contains invalid character(s): '%s'\n", program, text);
            return -1;
        }
        if (lastWasSpecial) {
            fprintf(stderr, "%s: '/' specifier only allowed with the last value\n", program);
            return -1;
        }
        if (kind == '/') {
            if (value == 0) {
                fprintf(stderr, "%s: tab size cannot be 0\n", program);
                return -1;
            }
            t->extend = value;
            lastWasSpecial = 1;
        } else if (kind == '+') {
            if (value == 0) {
                fprintf(stderr, "%s: tab size cannot be 0\n", program);
                return -1;
            }
            t->increment = value;
            lastWasSpecial = 1;
        } else if (add_stop(program, t, value, text) != 0) {
            return -1;
        }
    }
    return 0;
}

void tabstops_finish(TabStops* t) {
    if (t->count == 1 && t->extend == 0 && t->increment == 0) {
        t->tabSize = t->stops[0];
        t->count = 0;
        free(t->stops);
        t->stops = NULL;
    } else if (t->count == 0 && (t->extend > 0 || t->increment > 0)) {
        t->tabSize = t->extend > 0 ? t->extend : t->increment;
        t->extend = t->increment = 0;
    }
}

long long tabstops_next(const TabStops* t, long long column) {
    unsigned long i;

    if (t->count == 0)
        return column + t->tabSize - column % t->tabSize;
    for (i = 0; i < t->count; i++)
        if (t->stops[i] > column)
            return t->stops[i];
    {
        long long last = t->stops[t->count - 1];

        if (t->extend > 0)
            return column + t->extend - column % t->extend;     // multiples of N
        if (t->increment > 0)
            return last + ((column - last) / t->increment + 1) * t->increment;
    }
    return column + 1;
}

int tabstops_beyond(const TabStops* t, long long column) {
    return t->count > 0 && t->extend == 0 && t->increment == 0 && column >= t->stops[t->count - 1];
}
