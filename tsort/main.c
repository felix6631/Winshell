/*
 * tsort: topological sort. The input is pairs of names ("a b" means a comes before b); the output is every name once,
 * in an order that keeps all the pairs. It follows GNU tsort step by step (names are taken in the order of strcmp,
 * a name is written as soon as nothing has to come before it), including what it does with a loop: the loop is
 * reported on standard error, one pair of it is dropped and the sorting goes on, and the status is 1.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "args.h"
#include "fsutil.h"
#include "textutil.h"

#define VERSION "0.1"
#define PROGRAM "tsort"

static void print_usage(void) {
    puts("Usage: tsort [OPTION] [FILE]\n"
         "Write totally ordered list consistent with the partial ordering in FILE.\n"
         "\n"
         "With no FILE, or when FILE is -, read standard input.\n"
         "\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit");
}

static const ArgsOption options[] = { { 0 } };
static const ArgsSpec spec = { PROGRAM, VERSION, print_usage, options, 1, 0, 0 };

static int on_option(int id, const char* value, void* context) {
    (void)id;
    (void)value;
    (void)context;
    return 0;
}

typedef struct Successor {
    struct Successor* next;
    int               to;
} Successor;

typedef struct {
    char*      name;        // NULL once written
    size_t     count;       // names that have to come before it and are not written
    int        qlink;       // the queue of names that can be written, and the chain of the loop search; -1 none
    Successor* top;         // the names that come after it, the last pair read first
    unsigned   hash;
} Item;

typedef struct {
    Item*   items;
    size_t  count, capacity;
    int*    table;          // open addressing: index into items or -1
    size_t  tableSize;
} Graph;

static void die_memory(void) {
    fprintf(stderr, PROGRAM ": memory exhausted\n");
    exit(1);
}

static unsigned hash_bytes(const char* s) {
    unsigned h = 2166136261u;

    while (*s != '\0')
        h = (h ^ (unsigned char)*s++) * 16777619u;
    return h;
}

static void table_insert(Graph* g, int index) {
    size_t slot = g->items[index].hash & (g->tableSize - 1);

    while (g->table[slot] >= 0)
        slot = (slot + 1) & (g->tableSize - 1);
    g->table[slot] = index;
}

static int graph_lookup(Graph* g, const char* name) {
    unsigned h = hash_bytes(name);
    size_t slot;

    if (g->tableSize == 0) {
        g->tableSize = 1024;
        g->table = malloc(g->tableSize * sizeof *g->table);
        if (g->table == NULL)
            die_memory();
        memset(g->table, 0xFF, g->tableSize * sizeof *g->table);
    }
    slot = h & (g->tableSize - 1);
    while (g->table[slot] >= 0) {
        Item* it = &g->items[g->table[slot]];

        if (it->hash == h && strcmp(it->name, name) == 0)
            return g->table[slot];
        slot = (slot + 1) & (g->tableSize - 1);
    }
    if (g->count == g->capacity) {
        size_t capacity = g->capacity == 0 ? 256 : g->capacity * 2;
        Item* grown = realloc(g->items, capacity * sizeof *grown);

        if (grown == NULL)
            die_memory();
        g->items = grown;
        g->capacity = capacity;
    }
    {
        Item* it = &g->items[g->count];

        it->name = _strdup(name);
        if (it->name == NULL)
            die_memory();
        it->count = 0;
        it->qlink = -1;
        it->top = NULL;
        it->hash = h;
    }
    g->count++;
    if (g->count * 2 > g->tableSize) {
        size_t i;

        free(g->table);
        g->tableSize *= 2;
        g->table = malloc(g->tableSize * sizeof *g->table);
        if (g->table == NULL)
            die_memory();
        memset(g->table, 0xFF, g->tableSize * sizeof *g->table);
        for (i = 0; i < g->count; i++)
            table_insert(g, (int)i);
    } else {
        table_insert(g, (int)g->count - 1);
    }
    return (int)g->count - 1;
}

static void record_relation(Graph* g, int j, int k) {
    Successor* s;

    if (j == k)
        return;
    g->items[k].count++;
    s = malloc(sizeof *s);
    if (s == NULL)
        die_memory();
    s->to = k;
    s->next = g->items[j].top;
    g->items[j].top = s;
}

static Graph* sort_graph;

static int compare_items(const void* a, const void* b) {
    return strcmp(sort_graph->items[*(const int*)a].name, sort_graph->items[*(const int*)b].name);
}

/** The name as an error message shows it: in single quotes when it has anything but plain characters. */
static void print_quoted(FILE* out, const char* name) {
    const char* p;
    int plain = *name != '\0' && strcmp(name, "-") != 0;

    for (p = name; *p != '\0'; p++) {
        unsigned char c = (unsigned char)*p;

        if (!(isalnum(c) || strchr("_./+,:@%=-", c) != NULL))
            plain = 0;
    }
    if (strcmp(name, "-") == 0)
        plain = 1;
    if (plain) {
        fputs(name, out);
        return;
    }
    fputc('\'', out);
    for (p = name; *p != '\0'; p++) {
        if (*p == '\'')
            fputs("'\\''", out);
        else
            fputc(*p, out);
    }
    fputc('\'', out);
}

/**
 * One walk over the names in order, looking for a loop among the names that are not written yet (every one of them
 * has a name before it that is not written either). Returns 1 when a loop was found, written to standard error,
 * and one pair of it was dropped.
 */
static int detect_loop(Graph* g, const int* order, int* loop) {
    size_t n;

    for (n = 0; n < g->count; n++) {
        Item* k = &g->items[order[n]];
        Successor** p;

        if (k->count == 0 || k->name == NULL)
            continue;
        if (*loop < 0) {
            *loop = order[n];
            continue;
        }
        for (p = &k->top; *p != NULL; p = &(*p)->next) {
            if ((*p)->to != *loop)
                continue;
            if (k->qlink >= 0) {
                int node = *loop;
                int kIndex = order[n];

                // a loop: from the newest name back to this one, the pair of this name to the newest is dropped
                // (the whole chain is taken apart, also the part before it that is not a part of the loop)
                int found = 0;

                while (node >= 0) {
                    Item* it = &g->items[node];
                    int next = it->qlink;

                    if (!found)
                        fprintf(stderr, PROGRAM ": %s\n", it->name);
                    if (node == kIndex)
                        found = 1;
                    it->qlink = -1;
                    node = next;
                }
                g->items[*loop].count--;
                {
                    Successor* dropped = *p;

                    *p = dropped->next;
                    free(dropped);
                }
                *loop = -1;
                return 1;
            }
            k->qlink = *loop;
            *loop = order[n];
        }
    }
    return 0;
}

static int sort_file(const char* file, FILE* in) {
    Graph g;
    unsigned char* text;
    size_t length, pos = 0, i, remaining;
    int j = -1, ok = 1, head, zeros, loop = -1;
    int* order;
    char* token = NULL;
    size_t tokenCapacity = 0;

    memset(&g, 0, sizeof g);
    text = text_slurp(in, &length);
    if (text == NULL) {
        fprintf(stderr, PROGRAM ": ");
        print_quoted(stderr, file);
        fprintf(stderr, ": read error: %s\n", in == stdin ? "Input/output error" : "Is a directory");
        return 1;
    }
    while (pos < length) {
        size_t start, end;

        while (pos < length && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' ||
                                (text[pos] == '\r' && pos + 1 < length && text[pos + 1] == '\n')))
            pos++;
        if (pos >= length)
            break;
        start = pos;
        while (pos < length && text[pos] != ' ' && text[pos] != '\t' && text[pos] != '\n' &&
               !(text[pos] == '\r' && pos + 1 < length && text[pos + 1] == '\n'))
            pos++;
        end = pos;
        if (end - start + 1 > tokenCapacity) {
            tokenCapacity = end - start + 64;
            token = realloc(token, tokenCapacity);
            if (token == NULL)
                die_memory();
        }
        memcpy(token, text + start, end - start);
        token[end - start] = '\0';
        if (j < 0) {
            j = graph_lookup(&g, token);
        } else {
            int k = graph_lookup(&g, token);

            record_relation(&g, j, k);
            j = -1;
        }
    }
    free(token);
    free(text);
    if (j >= 0) {
        fprintf(stderr, PROGRAM ": ");
        print_quoted(stderr, file);
        fprintf(stderr, ": input contains an odd number of tokens\n");
        return 1;
    }

    order = malloc((g.count + 1) * sizeof *order);
    if (order == NULL)
        die_memory();
    for (i = 0; i < g.count; i++)
        order[i] = (int)i;
    sort_graph = &g;
    qsort(order, g.count, sizeof *order, compare_items);

    remaining = g.count;
    while (remaining > 0) {
        head = zeros = -1;
        for (i = 0; i < g.count; i++) {
            Item* k = &g.items[order[i]];

            if (k->count == 0 && k->name != NULL) {
                if (head < 0)
                    head = order[i];
                else
                    g.items[zeros].qlink = order[i];
                zeros = order[i];
            }
        }
        while (head >= 0) {
            Item* h = &g.items[head];
            Successor* p;

            fputs(h->name, stdout);
            fputc('\n', stdout);
            free(h->name);
            h->name = NULL;
            remaining--;
            for (p = h->top; p != NULL; p = p->next) {
                Item* s = &g.items[p->to];

                if (--s->count == 0) {
                    g.items[zeros].qlink = p->to;
                    zeros = p->to;
                }
            }
            head = h->qlink;
        }
        if (remaining > 0) {
            fflush(stdout);
            fprintf(stderr, PROGRAM ": ");
            print_quoted(stderr, file);
            fprintf(stderr, ": input contains a loop:\n");
            ok = 0;
            loop = -1;
            for (i = 0; i < 2 * g.count + 2 && !detect_loop(&g, order, &loop); i++)
                ;
        }
    }
    free(order);
    return ok ? 0 : 1;
}

int main(int argc, char* argv[]) {
    const char** operands;
    int count, result, status;
    FILE* in;

    text_binary_streams();
    result = args_parse(&spec, argc, argv, on_option, NULL, &operands, &count);
    if (result != ARGS_OK)
        return ARGS_STATUS(spec, result);
    if (count > 1) {
        fprintf(stderr, PROGRAM ": extra operand '%s'\n", operands[1]);
        args_usage_error(&spec);
        return 1;
    }
    count = (int)text_default_operands(operands, (size_t)count);
    if (text_open(PROGRAM, operands[0], 1, &in) != 0)
        return 1;
    status = sort_file(operands[0], in);
    if (in != stdin)
        fclose(in);
    if (fflush(stdout) != 0) {
        fprintf(stderr, PROGRAM ": write error\n");
        return 1;
    }
    return status;
}
