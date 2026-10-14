/*
 * Reading the files into lines, comparing lines through ids, and the longest common subsequence of the
 * two sequences of ids, by the algorithm of GNU diff (Myers, the middle snake in linear space) with its
 * choices and its shifting of the runs of changes, so that where several results are equally short the
 * same one is reported.
 */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "textutil.h"
#include "diff.h"

/* ---- lines ------------------------------------------------------------- */

int text_load(Text* t, FILE* in, int stripCr) {
    size_t offset = 0, capacity = 0, n = 0;
    TextLine line;

    memset(t, 0, sizeof *t);
    t->text = text_slurp(in, &t->length);
    if (t->text == NULL)
        return -1;
    t->binary = memchr(t->text, 0, t->length) != NULL;
    while (text_next_line(t->text, t->length, &offset, &line)) {
        if (n == capacity) {
            Span* grown;
            capacity = capacity == 0 ? 256 : capacity * 2;
            grown = realloc(t->lines, capacity * sizeof *grown);
            if (grown == NULL)
                return -1;
            t->lines = grown;
        }
        t->lines[n].start = (size_t)(line.content - t->text);
        t->lines[n].length = line.length;
        if (line.ending == 2 && !stripCr)
            t->lines[n].length++;               // the CR stays a part of the line
        t->noNewline = line.ending == 0;
        n++;
    }
    t->count = n;
    return 0;
}

void text_free(Text* t) {
    free(t->text);
    free(t->lines);
    free(t->ids);
    if (t->changed != NULL)
        free(t->changed - 1);
    memset(t, 0, sizeof *t);
}

/* ---- ids --------------------------------------------------------------- */

typedef struct {
    uint64_t       hash;
    unsigned char* key;
    size_t         length;
    int            id;              // 0: free
} Slot;

typedef struct {
    Slot*  slots;
    size_t size;                    // a power of two
    int    next;
} Classes;

static uint64_t fnv(const unsigned char* data, size_t length) {
    uint64_t h = 1469598103934665603ULL;
    size_t i;

    for (i = 0; i < length; i++)
        h = (h ^ data[i]) * 1099511628211ULL;
    return h;
}

static int class_of(Classes* c, const unsigned char* key, size_t length) {
    uint64_t h = fnv(key, length);
    size_t i = (size_t)h & (c->size - 1);

    for (; c->slots[i].id != 0; i = (i + 1) & (c->size - 1))
        if (c->slots[i].hash == h && c->slots[i].length == length && memcmp(c->slots[i].key, key, length) == 0)
            return c->slots[i].id;
    c->slots[i].key = malloc(length + 1);
    if (c->slots[i].key == NULL)
        return -1;
    memcpy(c->slots[i].key, key, length);
    c->slots[i].hash = h;
    c->slots[i].length = length;
    c->slots[i].id = c->next++;
    return c->slots[i].id;
}

static int is_blank_char(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\v' || c == '\f' || c == '\r';
}

/** The key of a line under the settings, into key (at least length + 2 bytes); returns its size. */
static size_t line_key(const Text* t, size_t index, const Settings* s, unsigned char* key) {
    const unsigned char* p = t->text + t->lines[index].start;
    size_t length = t->lines[index].length, i, n = 0;
    int pending = 0;

    if (s->stripTrailingCr && length > 0 && p[length - 1] == '\r')
        length--;
    for (i = 0; i < length; i++) {
        unsigned char c = p[i];

        if (s->ignoreAllSpace && is_blank_char(c))
            continue;
        if (s->ignoreSpaceChange && is_blank_char(c)) {
            pending = 1;
            continue;
        }
        if (pending) {
            key[n++] = ' ';
            pending = 0;
        }
        key[n++] = s->ignoreCase ? (unsigned char)tolower(c) : c;
    }
    if (index + 1 == t->count && t->noNewline && !s->ignoreAllSpace && !s->ignoreSpaceChange)
        key[n++] = 0;               // a last line without its line feed is not the same line (but white space is)
    return n;
}

int text_prepare(Text* a, Text* b, const Settings* settings) {
    Classes classes;
    Text* texts[2];
    size_t total = a->count + b->count, longest = 0, t, i;
    unsigned char* key;

    for (t = 0; t < 2; t++) {
        Text* x = t == 0 ? a : b;
        for (i = 0; i < x->count; i++)
            if (x->lines[i].length > longest)
                longest = x->lines[i].length;
    }
    texts[0] = a;
    texts[1] = b;
    classes.size = 16;
    while (classes.size < total * 2)
        classes.size *= 2;
    classes.slots = calloc(classes.size, sizeof *classes.slots);
    classes.next = 1;
    key = malloc(longest + 2);
    if (classes.slots == NULL || key == NULL)
        return -1;
    for (t = 0; t < 2; t++) {
        Text* x = texts[t];

        free(x->ids);
        x->ids = malloc((x->count + 1) * sizeof *x->ids);
        if (x->ids == NULL)
            return -1;
        for (i = 0; i < x->count; i++) {
            size_t n = line_key(x, i, settings, key);
            x->ids[i] = class_of(&classes, key, n);
            if (x->ids[i] < 0)
                return -1;
        }
    }
    for (i = 0; i < classes.size; i++)
        free(classes.slots[i].key);
    free(classes.slots);
    free(key);
    return 0;
}

/* ---- the common sequence ----------------------------------------------- */

/*
 * This is the search of GNU diff (diffseq.h of gnulib), step by step, so that where several shortest
 * edit scripts exist the same one is chosen: both ends are searched at once on the diagonals, the
 * halves are searched again, and past a cost of 200 the heuristics of GNU diff (a long snake that has
 * made much progress, then "too expensive") cut the search short.
 */

#define SNAKE_LIMIT 20

typedef struct {
    const int* a;
    const int* b;
    char*      changedA;
    char*      changedB;
    const long* mapA;               // the index in the file of a line of the reduced sequence
    const long* mapB;
    long*      forward;             // indexed by diagonal, x - y
    long*      backward;
    long       tooExpensive;
} Search;

typedef struct {
    long xmid, ymid;
    int  loMinimal, hiMinimal;
} Partition;

static long long_min(long x, long y) { return x < y ? x : y; }
static long long_max(long x, long y) { return x > y ? x : y; }

#define EQUAL(x, y) (s->a[x] == s->b[y])

/** Tries the two heuristics of GNU diff for a search that has gone on for c steps; true when one found a split. */
static int heuristic_split(const Search* s, long xoff, long xlim, long yoff, long ylim, long c,
                           long fmin, long fmax, long bmin, long bmax, long fmid, long bmid, Partition* part) {
    long best = 0, d;

    for (d = fmax; d >= fmin; d -= 2) {
        long dd = d - fmid, x = s->forward[d], y = x - d, v = (x - xoff) * 2 - dd;

        if (v > 12 * (c + (dd < 0 ? -dd : dd)) && v > best && xoff + SNAKE_LIMIT <= x && x < xlim &&
            yoff + SNAKE_LIMIT <= y && y < ylim) {
            int k;
            for (k = 1; EQUAL(x - k, y - k); k++)
                if (k == SNAKE_LIMIT) {
                    best = v;
                    part->xmid = x;
                    part->ymid = y;
                    break;
                }
        }
    }
    if (best > 0) {
        part->loMinimal = 1;
        part->hiMinimal = 0;
        return 1;
    }
    best = 0;
    for (d = bmax; d >= bmin; d -= 2) {
        long dd = d - bmid, x = s->backward[d], y = x - d, v = (xlim - x) * 2 + dd;

        if (v > 12 * (c + (dd < 0 ? -dd : dd)) && v > best && xoff < x && x <= xlim - SNAKE_LIMIT &&
            yoff < y && y <= ylim - SNAKE_LIMIT) {
            int k;
            for (k = 0; EQUAL(x + k, y + k); k++)
                if (k == SNAKE_LIMIT - 1) {
                    best = v;
                    part->xmid = x;
                    part->ymid = y;
                    break;
                }
        }
    }
    if (best > 0) {
        part->loMinimal = 0;
        part->hiMinimal = 1;
        return 1;
    }
    return 0;
}

/** The search has cost too much: split where the two directions have got furthest. */
static void give_up(const Search* s, long xoff, long xlim, long yoff, long ylim,
                    long fmin, long fmax, long bmin, long bmax, Partition* part) {
    long fxybest = -1, fxbest = 0, bxybest = LONG_MAX, bxbest = 0, d;

    for (d = fmax; d >= fmin; d -= 2) {
        long x = long_min(s->forward[d], xlim), y = x - d;

        if (ylim < y) {
            x = ylim + d;
            y = ylim;
        }
        if (fxybest < x + y) {
            fxybest = x + y;
            fxbest = x;
        }
    }
    for (d = bmax; d >= bmin; d -= 2) {
        long x = long_max(xoff, s->backward[d]), y = x - d;

        if (y < yoff) {
            x = yoff + d;
            y = yoff;
        }
        if (x + y < bxybest) {
            bxybest = x + y;
            bxbest = x;
        }
    }
    if ((xlim + ylim) - bxybest < fxybest - (xoff + yoff)) {
        part->xmid = fxbest;
        part->ymid = fxybest - fxbest;
        part->loMinimal = 1;
        part->hiMinimal = 0;
    } else {
        part->xmid = bxbest;
        part->ymid = bxybest - bxbest;
        part->loMinimal = 0;
        part->hiMinimal = 1;
    }
}

static void find_split(const Search* s, long xoff, long xlim, long yoff, long ylim, int findMinimal, Partition* part) {
    long* fd = s->forward;
    long* bd = s->backward;
    long dmin = xoff - ylim, dmax = xlim - yoff, fmid = xoff - yoff, bmid = xlim - ylim;
    long fmin = fmid, fmax = fmid, bmin = bmid, bmax = bmid, c, d;
    int odd = (int)((fmid - bmid) & 1);

    fd[fmid] = xoff;
    bd[bmid] = xlim;
    for (c = 1;; ++c) {
        int bigSnake = 0;

        if (fmin > dmin)
            fd[--fmin - 1] = -1;
        else
            ++fmin;
        if (fmax < dmax)
            fd[++fmax + 1] = -1;
        else
            --fmax;
        for (d = fmax; d >= fmin; d -= 2) {
            long tlo = fd[d - 1], thi = fd[d + 1];
            long x0 = tlo < thi ? thi : tlo + 1, x, y;

            for (x = x0, y = x0 - d; x < xlim && y < ylim && EQUAL(x, y); x++, y++)
                ;
            if (x - x0 > SNAKE_LIMIT)
                bigSnake = 1;
            fd[d] = x;
            if (odd && bmin <= d && d <= bmax && bd[d] <= x) {
                part->xmid = x;
                part->ymid = y;
                part->loMinimal = part->hiMinimal = 1;
                return;
            }
        }
        if (bmin > dmin)
            bd[--bmin - 1] = LONG_MAX;
        else
            ++bmin;
        if (bmax < dmax)
            bd[++bmax + 1] = LONG_MAX;
        else
            --bmax;
        for (d = bmax; d >= bmin; d -= 2) {
            long tlo = bd[d - 1], thi = bd[d + 1];
            long x0 = tlo < thi ? tlo : thi - 1, x, y;

            for (x = x0, y = x0 - d; xoff < x && yoff < y && EQUAL(x - 1, y - 1); x--, y--)
                ;
            if (x0 - x > SNAKE_LIMIT)
                bigSnake = 1;
            bd[d] = x;
            if (!odd && fmin <= d && d <= fmax && x <= fd[d]) {
                part->xmid = x;
                part->ymid = y;
                part->loMinimal = part->hiMinimal = 1;
                return;
            }
        }
        if (findMinimal)
            continue;
        if (c > 200 && bigSnake &&
            heuristic_split(s, xoff, xlim, yoff, ylim, c, fmin, fmax, bmin, bmax, fmid, bmid, part))
            return;
        if (c >= s->tooExpensive) {
            give_up(s, xoff, xlim, yoff, ylim, fmin, fmax, bmin, bmax, part);
            return;
        }
    }
}

static void compare_sequences(Search* s, long xoff, long xlim, long yoff, long ylim, int findMinimal) {
    while (xoff < xlim && yoff < ylim && EQUAL(xoff, yoff)) {
        xoff++;
        yoff++;
    }
    while (xoff < xlim && yoff < ylim && EQUAL(xlim - 1, ylim - 1)) {
        xlim--;
        ylim--;
    }
    if (xoff == xlim) {
        for (; yoff < ylim; yoff++)
            s->changedB[s->mapB[yoff]] = 1;
    } else if (yoff == ylim) {
        for (; xoff < xlim; xoff++)
            s->changedA[s->mapA[xoff]] = 1;
    } else {
        Partition part;

        find_split(s, xoff, xlim, yoff, ylim, findMinimal, &part);
        compare_sequences(s, xoff, part.xmid, yoff, part.ymid, part.loMinimal);
        compare_sequences(s, part.xmid, xlim, part.ymid, ylim, part.hiMinimal);
    }
}

/*
 * The runs of changed lines are moved as GNU diff does (shift_boundaries): back as far as the lines
 * allow, then forward, so that runs merge, and finally back to where the other file has a change.
 * changed[-1] and changed[count] are 0.
 */
static void shift_boundaries(const int* equivs, char* changed, const char* otherChanged, long count) {
    long i = 0, j = 0;

    for (;;) {
        long runLength, start, corresponding;

        while (i < count && !changed[i]) {
            while (otherChanged[j++])
                ;
            i++;
        }
        if (i == count)
            break;
        start = i;
        while (changed[++i])
            ;
        while (otherChanged[j])
            j++;
        do {
            runLength = i - start;
            while (start > 0 && equivs[start - 1] == equivs[i - 1]) {
                changed[--start] = 1;
                changed[--i] = 0;
                while (changed[start - 1])
                    start--;
                while (otherChanged[--j])
                    ;
            }
            corresponding = otherChanged[j - 1] ? i : count;
            while (i != count && equivs[start] == equivs[i]) {
                changed[start++] = 0;
                changed[i++] = 1;
                while (changed[i])
                    i++;
                while (otherChanged[++j])
                    corresponding = i;
            }
        } while (runLength != i - start);
        while (corresponding < i) {
            changed[--start] = 1;
            changed[--i] = 0;
            while (otherChanged[--j])
                ;
        }
    }
}

/** Lines that have no equal in the other file (1), or very many (2, provisionally): the discards of GNU diff. */
static void classify_lines(const int* ids, long n, const long* otherCount, char* discards) {
    long many = 5, tem = n / 64, i;

    while ((tem >>= 2) > 0)
        many *= 2;
    for (i = 0; i < n; i++) {
        long matches = otherCount[ids[i]];
        discards[i] = matches == 0 ? 1 : (matches > many ? 2 : 0);
    }
}

/** Keeps a provisional discard only inside a run of discards that starts and ends with a certain one. */
static void settle_run(char* discards, long i, long length, long provisional) {
    long j, consec, minimum = 1, tem = length >> 2;

    if (provisional * 4 > length) {
        for (j = i + length; j > i;)
            if (discards[--j] == 2)
                discards[j] = 0;
        return;
    }
    while (0 < (tem >>= 2))
        minimum <<= 1;
    minimum++;
    for (j = 0, consec = 0; j < length; j++) {
        if (discards[i + j] != 2)
            consec = 0;
        else if (minimum == ++consec)
            j -= consec;
        else if (minimum < consec)
            discards[i + j] = 0;
    }
    for (j = 0, consec = 0; j < length; j++) {
        if (j >= 8 && discards[i + j] == 1)
            break;
        if (discards[i + j] == 2) {
            consec = 0;
            discards[i + j] = 0;
        } else if (discards[i + j] == 0) {
            consec = 0;
        } else {
            consec++;
        }
        if (consec == 3)
            break;
    }
    i += length - 1;
    for (j = 0, consec = 0; j < length; j++) {
        if (j >= 8 && discards[i - j] == 1)
            break;
        if (discards[i - j] == 2) {
            consec = 0;
            discards[i - j] = 0;
        } else if (discards[i - j] == 0) {
            consec = 0;
        } else {
            consec++;
        }
        if (consec == 3)
            break;
    }
}

static void settle_discards(char* discards, long n) {
    long i;

    for (i = 0; i < n; i++) {
        if (discards[i] == 2) {
            discards[i] = 0;
        } else if (discards[i] != 0) {
            long j, length, provisional = 0;

            for (j = i; j < n; j++) {
                if (discards[j] == 0)
                    break;
                if (discards[j] == 2)
                    ++provisional;
            }
            while (j > i && discards[j - 1] == 2) {
                discards[--j] = 0;
                --provisional;
            }
            length = j - i;
            settle_run(discards, i, length, provisional);
            i += length - 1;
        }
    }
}

/** The lines that are not discarded: their ids and their positions; the discarded ones are marked changed. */
static long reduce(const int* ids, long n, const char* discards, char* changed, int* reduced, long* map) {
    long i, kept = 0;

    for (i = 0; i < n; i++) {
        if (discards[i]) {
            changed[i] = 1;
        } else {
            reduced[kept] = ids[i];
            map[kept++] = i;
        }
    }
    return kept;
}

/** The common sequence of two ranges of ids; the flags of the lines that are not in it are set (they are 0 before). */
static int diff_range(const int* idsOfA, long n, char* changedOfA, const int* idsOfB, long m, char* changedOfB) {
    Search s;
    long diagonals, tooExpensive = 1, i, classes = 1, reducedN, reducedM;
    long *buffer = NULL, *countA = NULL, *countB = NULL, *mapA = NULL, *mapB = NULL;
    int *idsA = NULL, *idsB = NULL;
    char *discardsA = NULL, *discardsB = NULL;
    int result = -1;

    for (i = 0; i < n; i++)
        classes = long_max(classes, idsOfA[i] + 1);
    for (i = 0; i < m; i++)
        classes = long_max(classes, idsOfB[i] + 1);
    countA = calloc((size_t)classes, sizeof *countA);
    countB = calloc((size_t)classes, sizeof *countB);
    discardsA = calloc((size_t)n + 1, 1);
    discardsB = calloc((size_t)m + 1, 1);
    idsA = malloc(((size_t)n + 1) * sizeof *idsA);
    idsB = malloc(((size_t)m + 1) * sizeof *idsB);
    mapA = malloc(((size_t)n + 1) * sizeof *mapA);
    mapB = malloc(((size_t)m + 1) * sizeof *mapB);
    if (countA == NULL || countB == NULL || discardsA == NULL || discardsB == NULL || idsA == NULL || idsB == NULL ||
        mapA == NULL || mapB == NULL)
        goto done;
    for (i = 0; i < n; i++)
        countA[idsOfA[i]]++;
    for (i = 0; i < m; i++)
        countB[idsOfB[i]]++;
    classify_lines(idsOfA, n, countB, discardsA);
    classify_lines(idsOfB, m, countA, discardsB);
    settle_discards(discardsA, n);
    settle_discards(discardsB, m);
    reducedN = reduce(idsOfA, n, discardsA, changedOfA, idsA, mapA);
    reducedM = reduce(idsOfB, m, discardsB, changedOfB, idsB, mapB);

    diagonals = reducedN + reducedM + 3;
    buffer = malloc((size_t)(diagonals * 2) * sizeof *buffer);
    if (buffer == NULL)
        goto done;
    for (i = diagonals; i > 0; i >>= 2)
        tooExpensive <<= 1;
    s.tooExpensive = long_max(4096, tooExpensive);
    s.forward = buffer + reducedM + 1;
    s.backward = buffer + diagonals + reducedM + 1;
    s.a = idsA;
    s.b = idsB;
    s.mapA = mapA;
    s.mapB = mapB;
    s.changedA = changedOfA;
    s.changedB = changedOfB;
    compare_sequences(&s, 0, reducedN, 0, reducedM, 0);
    shift_boundaries(idsOfA, changedOfA, changedOfB, n);
    shift_boundaries(idsOfB, changedOfB, changedOfA, m);
    result = 0;
done:
    free(buffer);
    free(countA);
    free(countB);
    free(discardsA);
    free(discardsB);
    free(idsA);
    free(idsB);
    free(mapA);
    free(mapB);
    return result;
}


/** True when line i of a and line j of b are the same: the same bytes, and both or neither lack the line feed. */
static int same_line(const Text* a, size_t i, const Text* b, size_t j) {
    int lastA = i + 1 == a->count && a->noNewline, lastB = j + 1 == b->count && b->noNewline;

    return lastA == lastB && a->lines[i].length == b->lines[j].length &&
           memcmp(a->text + a->lines[i].start, b->text + b->lines[j].start, a->lines[i].length) == 0;
}

int diff_lines(Text* a, Text* b, long context) {
    long n = (long)a->count, m = (long)b->count, prefix = 0, suffix = 0, limit = n < m ? n : m;

    if (a->changed != NULL)
        free(a->changed - 1);
    if (b->changed != NULL)
        free(b->changed - 1);
    a->changed = calloc((size_t)n + 2, 1);
    b->changed = calloc((size_t)m + 2, 1);
    if (a->changed == NULL || b->changed == NULL)
        return -1;
    a->changed++;                               // a guard before the first line and after the last
    b->changed++;

    // GNU diff does not look at the lines the files share at their ends, except for `context` of them
    while (prefix < limit && same_line(a, (size_t)prefix, b, (size_t)prefix))
        prefix++;
    while (suffix < limit - prefix && same_line(a, (size_t)(n - 1 - suffix), b, (size_t)(m - 1 - suffix)))
        suffix++;
    prefix = prefix > context ? prefix - context : 0;
    suffix = suffix > context ? suffix - context : 0;
    return diff_range(a->ids + prefix, n - prefix - suffix, a->changed + prefix,
                      b->ids + prefix, m - prefix - suffix, b->changed + prefix);
}

/* ---- hunks ------------------------------------------------------------- */

static int is_blank_line(const Text* t, size_t index) {
    size_t i;

    for (i = 0; i < t->lines[index].length; i++)
        if (!is_blank_char(t->text[t->lines[index].start + i]))
            return 0;
    return 1;
}

static int hunk_is_blank(const Text* a, const Text* b, const Hunk* h) {
    size_t i;

    for (i = h->a0; i < h->a1; i++)
        if (!is_blank_line(a, i))
            return 0;
    for (i = h->b0; i < h->b1; i++)
        if (!is_blank_line(b, i))
            return 0;
    return 1;
}

int diff_hunks(const Text* a, const Text* b, const Settings* settings, Hunk** out, size_t* count) {
    size_t i = 0, j = 0, n = 0, capacity = 0;
    Hunk* hunks = NULL;

    while (i < a->count || j < b->count) {
        Hunk h;

        if (i < a->count && j < b->count && !a->changed[i] && !b->changed[j]) {
            i++;
            j++;
            continue;
        }
        h.a0 = i;
        h.b0 = j;
        while (i < a->count && a->changed[i])
            i++;
        while (j < b->count && b->changed[j])
            j++;
        h.a1 = i;
        h.b1 = j;
        if (i == h.a0 && j == h.b0)             // flags that do not fit the lines: nothing more to report
            break;
        if (settings->ignoreBlankLines && hunk_is_blank(a, b, &h))
            continue;
        if (n == capacity) {
            Hunk* grown;
            capacity = capacity == 0 ? 16 : capacity * 2;
            grown = realloc(hunks, capacity * sizeof *grown);
            if (grown == NULL) {
                free(hunks);
                return -1;
            }
            hunks = grown;
        }
        hunks[n++] = h;
    }
    *out = hunks;
    *count = n;
    return 0;
}
