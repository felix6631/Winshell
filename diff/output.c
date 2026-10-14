/*
 * The three formats of the changes: normal, unified and context. Lines are written with a line feed; a last
 * line that had none is followed by the note GNU diff writes.
 */

#include <stdio.h>
#include <string.h>
#include "diff.h"

static void put_line(const Text* t, size_t index, const char* prefix) {
    fputs(prefix, stdout);
    fwrite(t->text + t->lines[index].start, 1, t->lines[index].length, stdout);
    putchar('\n');
    if (index + 1 == t->count && t->noNewline)
        fputs("\\ No newline at end of file\n", stdout);
}

static void put_lines(const Text* t, size_t from, size_t to, const char* prefix) {
    for (; from < to; from++)
        put_line(t, from, prefix);
}

/* ---- normal ------------------------------------------------------------ */

static void normal_range(size_t first, size_t last) {
    if (first == last)
        printf("%lu", (unsigned long)first);
    else
        printf("%lu,%lu", (unsigned long)first, (unsigned long)last);
}

static void output_normal(const Text* a, const Text* b, const Hunk* hunks, size_t count) {
    size_t i;

    for (i = 0; i < count; i++) {
        const Hunk* h = &hunks[i];
        char op = h->a0 == h->a1 ? 'a' : (h->b0 == h->b1 ? 'd' : 'c');

        if (op == 'a')
            printf("%lu", (unsigned long)h->a0);
        else
            normal_range(h->a0 + 1, h->a1);
        putchar(op);
        if (op == 'd')
            printf("%lu", (unsigned long)h->b0);
        else
            normal_range(h->b0 + 1, h->b1);
        putchar('\n');
        put_lines(a, h->a0, h->a1, "< ");
        if (op == 'c')
            fputs("---\n", stdout);
        put_lines(b, h->b0, h->b1, "> ");
    }
}

/* ---- groups of hunks that share their context --------------------------- */

typedef struct {
    size_t firstHunk, lastHunk;         // inclusive
    size_t startA, endA, startB, endB;  // the lines shown, with their context
} Group;

/** The group that starts at hunk index `from`. */
static Group group_at(const Hunk* hunks, size_t count, size_t from, size_t aCount, size_t bCount, size_t context) {
    Group g;
    size_t last = from, before, after;

    while (last + 1 < count && hunks[last + 1].a0 - hunks[last].a1 <= 2 * context)
        last++;
    g.firstHunk = from;
    g.lastHunk = last;
    before = hunks[from].a0 < context ? hunks[from].a0 : context;
    after = aCount - hunks[last].a1 < context ? aCount - hunks[last].a1 : context;
    g.startA = hunks[from].a0 - before;
    g.startB = hunks[from].b0 - before;
    g.endA = hunks[last].a1 + after;
    g.endB = hunks[last].b1 + after;
    (void)bCount;
    return g;
}

/* ---- headings ---------------------------------------------------------- */

static void put_time(FILETIME ft) {
    SYSTEMTIME utc, local;
    FILETIME localFt;
    unsigned long long a, b, nanos;
    long long offset;
    char sign;

    FileTimeToSystemTime(&ft, &utc);
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, &local))
        local = utc;
    SystemTimeToFileTime(&local, &localFt);
    a = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    b = ((unsigned long long)localFt.dwHighDateTime << 32) | localFt.dwLowDateTime;
    offset = (long long)(b / 10000000ULL) - (long long)(a / 10000000ULL);
    sign = offset < 0 ? '-' : '+';
    if (offset < 0)
        offset = -offset;
    nanos = (a % 10000000ULL) * 100;
    printf("%04d-%02d-%02d %02d:%02d:%02d.%09llu %c%02lld%02lld", local.wYear, local.wMonth, local.wDay, local.wHour,
           local.wMinute, local.wSecond, nanos, sign, offset / 3600, offset / 60 % 60);
}

static void put_heading(const char* mark, const Heading* h) {
    printf("%s %s", mark, h->label);
    if (h->showTime) {
        putchar('\t');
        put_time(h->modified);
    }
    putchar('\n');
}

/* ---- unified ----------------------------------------------------------- */

static void unified_range(size_t start, size_t end) {
    size_t length = end - start;

    if (length == 1)
        printf("%lu", (unsigned long)(start + 1));
    else if (length == 0)
        printf("%lu,0", (unsigned long)start);
    else
        printf("%lu,%lu", (unsigned long)(start + 1), (unsigned long)length);
}

static void output_unified(const Settings* s, const Text* a, const Text* b, const Heading* h1, const Heading* h2,
                           const Hunk* hunks, size_t count) {
    size_t from = 0, context = (size_t)s->context;

    if (count == 0)
        return;
    put_heading("---", h1);
    put_heading("+++", h2);
    while (from < count) {
        Group g = group_at(hunks, count, from, a->count, b->count, context);
        size_t position = g.startA, i;

        fputs("@@ -", stdout);
        unified_range(g.startA, g.endA);
        fputs(" +", stdout);
        unified_range(g.startB, g.endB);
        fputs(" @@\n", stdout);
        for (i = g.firstHunk; i <= g.lastHunk; i++) {
            put_lines(a, position, hunks[i].a0, " ");
            put_lines(a, hunks[i].a0, hunks[i].a1, "-");
            put_lines(b, hunks[i].b0, hunks[i].b1, "+");
            position = hunks[i].a1;
        }
        put_lines(a, position, g.endA, " ");
        from = g.lastHunk + 1;
    }
}

/* ---- context ----------------------------------------------------------- */

static void context_range(size_t start, size_t end) {
    if (end - start <= 1)
        printf("%lu", (unsigned long)end);
    else
        printf("%lu,%lu", (unsigned long)(start + 1), (unsigned long)end);
}

/** One side of a group: the lines with ' ' for context and the mark of each change. */
static void context_side(const Text* t, const Hunk* hunks, const Group* g, int newSide) {
    size_t position = newSide ? g->startB : g->startA, i;
    size_t end = newSide ? g->endB : g->endA;

    for (i = g->firstHunk; i <= g->lastHunk; i++) {
        const Hunk* h = &hunks[i];
        size_t from = newSide ? h->b0 : h->a0, to = newSide ? h->b1 : h->a1;
        int both = h->a0 != h->a1 && h->b0 != h->b1;

        put_lines(t, position, from, "  ");
        put_lines(t, from, to, both ? "! " : (newSide ? "+ " : "- "));
        position = to;
    }
    put_lines(t, position, end, "  ");
}

static void output_context(const Settings* s, const Text* a, const Text* b, const Heading* h1, const Heading* h2,
                           const Hunk* hunks, size_t count) {
    size_t from = 0;

    if (count == 0)
        return;
    put_heading("***", h1);
    put_heading("---", h2);
    while (from < count) {
        Group g = group_at(hunks, count, from, a->count, b->count, (size_t)s->context);
        size_t i;
        int hasOld = 0, hasNew = 0;

        for (i = g.firstHunk; i <= g.lastHunk; i++) {
            hasOld |= hunks[i].a0 != hunks[i].a1;
            hasNew |= hunks[i].b0 != hunks[i].b1;
        }
        fputs("***************\n*** ", stdout);
        context_range(g.startA, g.endA);
        fputs(" ****\n", stdout);
        if (hasOld)
            context_side(a, hunks, &g, 0);
        fputs("--- ", stdout);
        context_range(g.startB, g.endB);
        fputs(" ----\n", stdout);
        if (hasNew)
            context_side(b, hunks, &g, 1);
        from = g.lastHunk + 1;
    }
}

void diff_output(const Settings* settings, const Text* a, const Text* b, const Heading* first, const Heading* second,
                 const Hunk* hunks, size_t count) {
    if (settings->format == FORMAT_UNIFIED)
        output_unified(settings, a, b, first, second, hunks, count);
    else if (settings->format == FORMAT_CONTEXT)
        output_context(settings, a, b, first, second, hunks, count);
    else
        output_normal(a, b, hunks, count);
}
