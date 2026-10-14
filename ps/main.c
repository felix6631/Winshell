/*
 * ps: a snapshot of the processes. Without a selection it lists the processes that share this console, which
 * is what the terminal is on Linux; -e lists all of them. The columns are the ones of procps that mean
 * something on Windows (pid ppid user comm args time etime start tty nlwp rss vsz pcpu pmem ni); a column
 * that cannot be known for a process (it is protected) shows "-" or "?".
 * The exit status is 0 when at least one process was selected, 1 when none was, 1 for a wrong option.
 */

#define PSAPI_VERSION 2
#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#define VERSION "0.1"
#define PROGRAM "ps"
#define LIST_MAX 256

static void print_usage(void) {
    puts("Usage: ps [OPTION]...\n"
         "Display information about processes. Without a selection: those on this console.\n"
         "\n"
         "Selection:\n"
         "  -e, -A, ax          all processes\n"
         "  -a, a               processes on a console of any user (the default selection)\n"
         "  x, -x               processes of the current user\n"
         "  -p, --pid LIST      by process ID (comma separated), also: p LIST\n"
         "      --ppid LIST     by parent process ID\n"
         "  -u, -U, --user LIST by user name\n"
         "  -C LIST             by command name (with or without .exe)\n"
         "  -N, --deselect      the processes the selection does not give\n"
         "Output:\n"
         "  -f                  full format: UID PID PPID C STIME TTY TIME CMD\n"
         "  u                   user-oriented format: USER PID %CPU %MEM VSZ RSS TTY START TIME COMMAND\n"
         "  -o, o, --format FMT columns by name: pid ppid user comm args cmd time etime stime start tty\n"
         "                      nlwp rss vsz pcpu pmem c ni; NAME=HEADING sets the heading\n"
         "      --sort KEYS     sort by columns, + or - in front of each (-pcpu,pid)\n"
         "      --no-headers    print no heading line (also: h)\n"
         "  -w, w               accepted, lines are never cut\n"
         "      --help          display this help and exit\n"
         "  -V, --version       output version information and exit\n"
         "\n"
         "TTY is con for the processes of this console and ? for the others; RSS and VSZ are the working\n"
         "set and the committed memory in KiB. Exit status: 0 if a process was selected, 1 if none.");
}

/* ---- processes --------------------------------------------------------- */

typedef struct {
    DWORD              pid, ppid, threads;
    char               name[MAX_PATH * 4];
    char               user[96];
    char*              args;                // the command line, NULL when it cannot be read
    int                haveTimes, haveMemory, haveNice;
    unsigned long long created;             // FILETIME: 100 ns since 1601
    unsigned long long cpu;                 // kernel + user time, 100 ns
    unsigned long long rss, vsz;            // KiB
    int                nice;
    int                onConsole, selected;
} Proc;

typedef struct {
    unsigned long long now;                 // FILETIME
    unsigned long long totalKiB;            // physical memory
} Clock;

static void utf8_from_wide(const wchar_t* wide, int count, char* out, size_t size) {
    int n = WideCharToMultiByte(CP_UTF8, 0, wide, count, out, (int)size - 1, NULL, NULL);
    out[n > 0 ? n : 0] = '\0';
}

static void account_of(HANDLE process, char* out, size_t size) {
    HANDLE token;
    BYTE buffer[512];
    DWORD length;
    wchar_t name[128], domain[128];
    DWORD nameSize = 128, domainSize = 128;
    SID_NAME_USE use;

    snprintf(out, size, "?");
    if (!OpenProcessToken(process, TOKEN_QUERY, &token))
        return;
    if (GetTokenInformation(token, TokenUser, buffer, sizeof buffer, &length) &&
        LookupAccountSidW(NULL, ((TOKEN_USER*)buffer)->User.Sid, name, &nameSize, domain, &domainSize, &use))
        utf8_from_wide(name, -1, out, size);
    CloseHandle(token);
}

typedef LONG (NTAPI* QueryInformation)(HANDLE, ULONG, PVOID, ULONG, PULONG);

typedef struct {
    USHORT  length, maximum;
    PWSTR   buffer;
} CountedString;

/** The command line of a process (ProcessCommandLineInformation, Windows 8.1 and later), or NULL. */
static char* command_line_of(HANDLE process) {
    static QueryInformation query;
    ULONG size = 1024, length = 0;
    BYTE* buffer;
    LONG status;
    char* text;
    const CountedString* counted;

    if (query == NULL) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll != NULL)
            query = (QueryInformation)(void*)GetProcAddress(ntdll, "NtQueryInformationProcess");
        if (query == NULL)
            return NULL;
    }
    for (;;) {
        buffer = malloc(size);
        if (buffer == NULL)
            return NULL;
        status = query(process, 60, buffer, size, &length);
        if (status >= 0)
            break;
        free(buffer);
        if (((ULONG)status != 0xC0000004UL && (ULONG)status != 0xC0000023UL && (ULONG)status != 0x80000005UL) ||
            length <= size || length > (1UL << 20))
            return NULL;
        size = length;
    }
    counted = (const CountedString*)buffer;
    if (counted->length == 0 || counted->buffer == NULL) {
        free(buffer);
        return NULL;
    }
    text = malloc((size_t)counted->length * 2 + 1);
    if (text != NULL)
        utf8_from_wide(counted->buffer, counted->length / 2, text, (size_t)counted->length * 2 + 1);
    free(buffer);
    return text;
}

static int nice_of(DWORD priorityClass) {
    switch (priorityClass) {
    case IDLE_PRIORITY_CLASS:         return 19;
    case BELOW_NORMAL_PRIORITY_CLASS: return 10;
    case NORMAL_PRIORITY_CLASS:       return 0;
    case ABOVE_NORMAL_PRIORITY_CLASS: return -5;
    case HIGH_PRIORITY_CLASS:         return -10;
    case REALTIME_PRIORITY_CLASS:     return -20;
    default:                          return 0;
    }
}

static unsigned long long as_number(FILETIME t) {
    return ((unsigned long long)t.dwHighDateTime << 32) | t.dwLowDateTime;
}

/** The cheap facts of a process: times, memory, priority; the user when asked for. */
static void load_details(Proc* p, int wantUser) {
    HANDLE h;
    FILETIME created, exited, kernel, user;
    PROCESS_MEMORY_COUNTERS memory;
    DWORD priority;

    snprintf(p->user, sizeof p->user, "?");
    if (p->pid == 0 || p->pid == 4) {
        snprintf(p->user, sizeof p->user, "SYSTEM");
        return;
    }
    h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p->pid);
    if (h == NULL)
        return;
    if (GetProcessTimes(h, &created, &exited, &kernel, &user)) {
        p->haveTimes = 1;
        p->created = as_number(created);
        p->cpu = as_number(kernel) + as_number(user);
    }
    memory.cb = sizeof memory;
    if (GetProcessMemoryInfo(h, &memory, sizeof memory)) {
        p->haveMemory = 1;
        p->rss = memory.WorkingSetSize / 1024;
        p->vsz = memory.PagefileUsage / 1024;
    }
    priority = GetPriorityClass(h);
    if (priority != 0) {
        p->haveNice = 1;
        p->nice = nice_of(priority);
    }
    if (wantUser)
        account_of(h, p->user, sizeof p->user);
    CloseHandle(h);
}

/* ---- columns ----------------------------------------------------------- */

typedef struct Field Field;

struct Field {
    const char* key;
    const char* header;
    int         right;                      // aligned to the right
    int         minimum;                    // least width
    void        (*text)(const Proc*, const Clock*, char*, size_t);
    int         (*number)(const Proc*, const Clock*, double*);     // for sorting; NULL for texts
};

static unsigned long long cpu_seconds(const Proc* p) {
    return p->cpu / 10000000ULL;
}

static unsigned long long elapsed_100ns(const Proc* p, const Clock* c) {
    return c->now > p->created ? c->now - p->created : 0;
}

static void text_pid(const Proc* p, const Clock* c, char* o, size_t n)   { (void)c; snprintf(o, n, "%lu", p->pid); }
static void text_ppid(const Proc* p, const Clock* c, char* o, size_t n)  { (void)c; snprintf(o, n, "%lu", p->ppid); }
static void text_user(const Proc* p, const Clock* c, char* o, size_t n)  { (void)c; snprintf(o, n, "%s", p->user); }
static void text_comm(const Proc* p, const Clock* c, char* o, size_t n)  { (void)c; snprintf(o, n, "%s", p->name); }

static void text_args(const Proc* p, const Clock* c, char* o, size_t n) {
    (void)c;
    if (p->args != NULL)
        snprintf(o, n, "%s", p->args);
    else if (p->name[0] == '[')
        snprintf(o, n, "%s", p->name);          // the System and Idle processes are named this way already
    else
        snprintf(o, n, "[%s]", p->name);
}

static void text_tty(const Proc* p, const Clock* c, char* o, size_t n) { (void)c; snprintf(o, n, "%s", p->onConsole ? "con" : "?"); }

static void text_time(const Proc* p, const Clock* c, char* o, size_t n) {
    unsigned long long s = cpu_seconds(p);

    (void)c;
    if (!p->haveTimes) { snprintf(o, n, "-"); return; }
    snprintf(o, n, "%02llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
}

static void text_bsdtime(const Proc* p, const Clock* c, char* o, size_t n) {
    unsigned long long s = cpu_seconds(p);

    (void)c;
    if (!p->haveTimes) { snprintf(o, n, "-"); return; }
    snprintf(o, n, "%llu:%02llu", s / 60, s % 60);
}

static void text_etime(const Proc* p, const Clock* c, char* o, size_t n) {
    unsigned long long s = elapsed_100ns(p, c) / 10000000ULL;

    if (!p->haveTimes) { snprintf(o, n, "-"); return; }
    if (s >= 86400)
        snprintf(o, n, "%llu-%02llu:%02llu:%02llu", s / 86400, s / 3600 % 24, s / 60 % 60, s % 60);
    else if (s >= 3600)
        snprintf(o, n, "%02llu:%02llu:%02llu", s / 3600, s / 60 % 60, s % 60);
    else
        snprintf(o, n, "%02llu:%02llu", s / 60, s % 60);
}

static void local_time(unsigned long long filetime, SYSTEMTIME* local) {
    FILETIME ft;
    SYSTEMTIME utc;

    ft.dwLowDateTime = (DWORD)(filetime & 0xFFFFFFFFULL);
    ft.dwHighDateTime = (DWORD)(filetime >> 32);
    FileTimeToSystemTime(&ft, &utc);
    if (!SystemTimeToTzSpecificLocalTime(NULL, &utc, local))
        *local = utc;
}

/** HH:MM for a process that started today, MonDD for an earlier day, the year after a year. */
static void text_start(const Proc* p, const Clock* c, char* o, size_t n) {
    static const char* months[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    SYSTEMTIME then, now;

    if (!p->haveTimes) { snprintf(o, n, "-"); return; }
    local_time(p->created, &then);
    local_time(c->now, &now);
    if (then.wYear == now.wYear && then.wMonth == now.wMonth && then.wDay == now.wDay)
        snprintf(o, n, "%02d:%02d", then.wHour, then.wMinute);
    else if (elapsed_100ns(p, c) > 365ULL * 86400 * 10000000ULL)
        snprintf(o, n, "%d", then.wYear);
    else
        snprintf(o, n, "%s%02d", months[(then.wMonth + 11) % 12], then.wDay);
}

static void text_nlwp(const Proc* p, const Clock* c, char* o, size_t n) { (void)c; snprintf(o, n, "%lu", p->threads); }

static void text_rss(const Proc* p, const Clock* c, char* o, size_t n) {
    (void)c;
    if (p->haveMemory) snprintf(o, n, "%llu", p->rss); else snprintf(o, n, "-");
}

static void text_vsz(const Proc* p, const Clock* c, char* o, size_t n) {
    (void)c;
    if (p->haveMemory) snprintf(o, n, "%llu", p->vsz); else snprintf(o, n, "-");
}

static int number_cpu(const Proc* p, const Clock* c, double* v) {
    unsigned long long elapsed = elapsed_100ns(p, c);

    if (!p->haveTimes || elapsed == 0)
        return 0;
    *v = (double)p->cpu * 100.0 / (double)elapsed;
    if (*v > 100.0 * 1024)
        *v = 100.0 * 1024;
    return 1;
}

static void text_pcpu(const Proc* p, const Clock* c, char* o, size_t n) {
    double v;
    if (number_cpu(p, c, &v)) snprintf(o, n, "%.1f", v); else snprintf(o, n, "-");
}

static void text_c(const Proc* p, const Clock* c, char* o, size_t n) {
    double v;
    if (number_cpu(p, c, &v)) snprintf(o, n, "%d", v > 99 ? 99 : (int)v); else snprintf(o, n, "-");
}

static int number_mem(const Proc* p, const Clock* c, double* v) {
    if (!p->haveMemory || c->totalKiB == 0)
        return 0;
    *v = (double)p->rss * 100.0 / (double)c->totalKiB;
    return 1;
}

static void text_pmem(const Proc* p, const Clock* c, char* o, size_t n) {
    double v;
    if (number_mem(p, c, &v)) snprintf(o, n, "%.1f", v); else snprintf(o, n, "-");
}

static void text_ni(const Proc* p, const Clock* c, char* o, size_t n) {
    (void)c;
    if (p->haveNice) snprintf(o, n, "%d", p->nice); else snprintf(o, n, "-");
}

static int number_pid(const Proc* p, const Clock* c, double* v)   { (void)c; *v = p->pid; return 1; }
static int number_ppid(const Proc* p, const Clock* c, double* v)  { (void)c; *v = p->ppid; return 1; }
static int number_nlwp(const Proc* p, const Clock* c, double* v)  { (void)c; *v = p->threads; return 1; }
static int number_rss(const Proc* p, const Clock* c, double* v)   { (void)c; *v = (double)p->rss; return p->haveMemory; }
static int number_vsz(const Proc* p, const Clock* c, double* v)   { (void)c; *v = (double)p->vsz; return p->haveMemory; }
static int number_time(const Proc* p, const Clock* c, double* v)  { (void)c; *v = (double)p->cpu; return p->haveTimes; }
static int number_start(const Proc* p, const Clock* c, double* v) { (void)c; *v = (double)p->created; return p->haveTimes; }
static int number_etime(const Proc* p, const Clock* c, double* v) { *v = (double)elapsed_100ns(p, c); return p->haveTimes; }
static int number_ni(const Proc* p, const Clock* c, double* v)    { (void)c; *v = p->nice; return p->haveNice; }

static const Field fields[] = {
    { "pid",     "PID",     1, 7, text_pid,     number_pid },
    { "ppid",    "PPID",    1, 7, text_ppid,    number_ppid },
    { "user",    "USER",    0, 8, text_user,    NULL },
    { "uid",     "UID",     0, 8, text_user,    NULL },
    { "comm",    "COMMAND", 0, 1, text_comm,    NULL },
    { "args",    "COMMAND", 0, 1, text_args,    NULL },
    { "cmd",     "CMD",     0, 1, text_args,    NULL },
    { "ccomm",   "CMD",     0, 1, text_comm,    NULL },
    { "time",    "TIME",    1, 8, text_time,    number_time },
    { "bsdtime", "TIME",    1, 6, text_bsdtime, number_time },
    { "etime",   "ELAPSED", 1, 11, text_etime,  number_etime },
    { "stime",   "STIME",   0, 5, text_start,   number_start },
    { "start",   "START",   0, 5, text_start,   number_start },
    { "tty",     "TTY",     0, 8, text_tty,     NULL },
    { "nlwp",    "NLWP",    1, 4, text_nlwp,    number_nlwp },
    { "rss",     "RSS",     1, 6, text_rss,     number_rss },
    { "vsz",     "VSZ",     1, 7, text_vsz,     number_vsz },
    { "pcpu",    "%CPU",    1, 4, text_pcpu,    number_cpu },
    { "pmem",    "%MEM",    1, 4, text_pmem,    number_mem },
    { "c",       "C",       1, 2, text_c,       number_cpu },
    { "ni",      "NI",      1, 3, text_ni,      number_ni }
};
#define FIELD_COUNT ((int)(sizeof fields / sizeof fields[0]))

static const char* const aliases[][2] = {
    { "cputime", "time" }, { "start_time", "stime" }, { "tt", "tty" }, { "tname", "tty" }, { "thcount", "nlwp" },
    { "rssize", "rss" }, { "rsz", "rss" }, { "vsize", "vsz" }, { "%cpu", "pcpu" }, { "%mem", "pmem" },
    { "command", "args" }, { "euser", "user" }, { "ruser", "user" }, { "uname", "user" }, { "ucmd", "comm" },
    { "ucomm", "comm" }, { "nice", "ni" }
};

static const Field* find_field(const char* key) {
    int i;

    for (i = 0; i < (int)(sizeof aliases / sizeof aliases[0]); i++)
        if (_stricmp(key, aliases[i][0]) == 0)
            key = aliases[i][1];
    for (i = 0; i < FIELD_COUNT; i++)
        if (_stricmp(key, fields[i].key) == 0)
            return &fields[i];
    return NULL;
}

/** A column of the output: the field and the heading that is shown. */
typedef struct {
    const Field* field;
    char         header[64];
} Column;

/* ---- options ----------------------------------------------------------- */

typedef struct {
    int    all, console, mine, deselect, noHeaders;
    int    full, bsdUser;
    DWORD  pids[LIST_MAX], parents[LIST_MAX];
    int    pidCount, parentCount;
    char*  users[LIST_MAX];
    char*  commands[LIST_MAX];
    int    userCount, commandCount;
    char   format[1024];                    // the -o lists, joined by commas
    char   sort[256];
} Options;

static int fail(const char* text, const char* detail) {
    fprintf(stderr, "%s: %s%s%s\n", PROGRAM, text, detail != NULL ? ": " : "", detail != NULL ? detail : "");
    return 1;
}

/** Splits "a,b c" in place; returns the number of items (up to max). */
static int split_list(char* text, char** items, int max) {
    int count = 0;
    char* next = NULL;
    char* token;

    for (token = strtok_s(text, ", \t", &next); token != NULL && count < max; token = strtok_s(NULL, ", \t", &next))
        items[count++] = token;
    return count;
}

static int add_numbers(const char* value, DWORD* out, int* count) {
    char* copy = _strdup(value);
    char* items[LIST_MAX];
    int n, i;

    if (copy == NULL)
        return 1;
    n = split_list(copy, items, LIST_MAX);
    for (i = 0; i < n && *count < LIST_MAX; i++) {
        char* end;
        unsigned long v = strtoul(items[i], &end, 10);

        if (*end != '\0')
            return fail("error: process ID list syntax error", NULL);
        out[(*count)++] = (DWORD)v;
    }
    return 0;
}

static int add_words(const char* value, char** out, int* count) {
    char* copy = _strdup(value);

    if (copy == NULL)
        return 1;
    *count += split_list(copy, out + *count, LIST_MAX - *count);
    return 0;
}

static int add_format(Options* o, const char* value) {
    size_t used = strlen(o->format);

    if (used + strlen(value) + 2 >= sizeof o->format)
        return fail("error: format string too long", NULL);
    snprintf(o->format + used, sizeof o->format - used, "%s%s", used > 0 ? "," : "", value);
    return 0;
}

/** Applies one option with its value (NULL when it takes none). Returns 0, 1 for an error. */
static int apply(Options* o, const char* name, const char* value) {
    if (strcmp(name, "p") == 0 || strcmp(name, "pid") == 0)
        return add_numbers(value, o->pids, &o->pidCount);
    if (strcmp(name, "ppid") == 0)
        return add_numbers(value, o->parents, &o->parentCount);
    if (strcmp(name, "u") == 0 || strcmp(name, "U") == 0 || strcmp(name, "user") == 0)
        return add_words(value, o->users, &o->userCount);
    if (strcmp(name, "C") == 0)
        return add_words(value, o->commands, &o->commandCount);
    if (strcmp(name, "o") == 0 || strcmp(name, "format") == 0)
        return add_format(o, value);
    if (strcmp(name, "sort") == 0) {
        size_t used = strlen(o->sort);
        snprintf(o->sort + used, sizeof o->sort - used, "%s%s", used > 0 ? "," : "", value);
        return 0;
    }
    return fail("error: unknown option", name);
}

static int takes_value(const char* name) {
    static const char* const names[] = { "p", "pid", "ppid", "u", "U", "user", "C", "o", "format", "sort" };
    int i;

    for (i = 0; i < (int)(sizeof names / sizeof names[0]); i++)
        if (strcmp(name, names[i]) == 0)
            return 1;
    return 0;
}

/** Returns -1 to go on, or the exit status of main. */
static int parse_arguments(Options* o, int argc, char* argv[]) {
    int i;

    for (i = 1; i < argc; i++) {
        const char* a = argv[i];

        if (strncmp(a, "--", 2) == 0) {
            char name[32];
            const char* equals = strchr(a + 2, '=');
            const char* value = equals != NULL ? equals + 1 : NULL;
            size_t length = equals != NULL ? (size_t)(equals - (a + 2)) : strlen(a + 2);

            snprintf(name, sizeof name, "%.*s", (int)(length < sizeof name ? length : sizeof name - 1), a + 2);
            if (strcmp(name, "help") == 0) { print_usage(); return 0; }
            if (strcmp(name, "version") == 0) { printf("%s %s\n", PROGRAM, VERSION); return 0; }
            if (strcmp(name, "no-headers") == 0 || strcmp(name, "noheaders") == 0) { o->noHeaders = 1; continue; }
            if (strcmp(name, "deselect") == 0) { o->deselect = 1; continue; }
            if (strcmp(name, "forest") == 0 || strcmp(name, "cols") == 0 || strcmp(name, "columns") == 0 ||
                strcmp(name, "width") == 0 || strcmp(name, "lines") == 0 || strcmp(name, "rows") == 0)
                continue;
            if (!takes_value(name))
                return fail("error: unknown gnu long option", a);
            if (value == NULL) {
                if (i + 1 >= argc)
                    return fail("error: option requires an argument", a);
                value = argv[++i];
            }
            if (apply(o, name, value) != 0)
                return 1;
        } else if (a[0] == '-' && a[1] != '\0') {
            const char* c;

            for (c = a + 1; *c != '\0'; c++) {
                char name[2] = { *c, '\0' };

                if (*c == 'e' || *c == 'A') o->all = 1;
                else if (*c == 'a') o->console = 1;
                else if (*c == 'x') o->mine = 1;
                else if (*c == 'f') o->full = 1;
                else if (*c == 'N') o->deselect = 1;
                else if (*c == 'w' || *c == 'H') continue;
                else if (*c == 'V') { printf("%s %s\n", PROGRAM, VERSION); return 0; }
                else if (takes_value(name)) {
                    const char* value = c[1] != '\0' ? c + 1 : (i + 1 < argc ? argv[++i] : NULL);

                    if (value == NULL)
                        return fail("error: option requires an argument", a);
                    if (apply(o, name, value) != 0)
                        return 1;
                    break;
                } else {
                    fprintf(stderr, "%s: error: unsupported option -- '%c'\n", PROGRAM, *c);
                    return 1;
                }
            }
        } else {
            const char* c;

            // BSD style: letters without a dash
            for (c = a; *c != '\0'; c++) {
                char name[2] = { *c, '\0' };

                if (*c == 'a') o->console = 1;
                else if (*c == 'x') o->mine = 1;
                else if (*c == 'u') o->bsdUser = 1;
                else if (*c == 'h') o->noHeaders = 1;
                else if (*c == 'w' || *c == 'e' || *c == 'f' || *c == 'r') continue;
                else if (*c == 'p' || *c == 'o') {
                    const char* value = c[1] != '\0' ? c + 1 : (i + 1 < argc ? argv[++i] : NULL);

                    if (value == NULL)
                        return fail("error: option requires an argument", name);
                    if (apply(o, name, value) != 0)
                        return 1;
                    break;
                } else {
                    return fail("error: garbage option", a);
                }
            }
        }
    }
    return -1;
}

/* ---- the table --------------------------------------------------------- */

/** The columns for the format given with -o, or for -f, u, or the default. Returns the count, -1 on error. */
static int build_columns(const Options* o, Column* columns, int max) {
    int count = 0;

    if (o->format[0] != '\0') {
        char* copy = _strdup(o->format);
        char* items[LIST_MAX];
        int n, i;

        if (copy == NULL)
            return -1;
        n = split_list(copy, items, LIST_MAX);
        for (i = 0; i < n && count < max; i++) {
            char* equals = strchr(items[i], '=');
            const Field* field;

            if (equals != NULL)
                *equals = '\0';
            field = find_field(items[i]);
            if (field == NULL) {
                fprintf(stderr, "%s: error: unknown user-defined format specifier \"%s\"\n", PROGRAM, items[i]);
                return -1;
            }
            columns[count].field = field;
            snprintf(columns[count].header, sizeof columns[count].header, "%s", equals != NULL ? equals + 1 : field->header);
            count++;
        }
        return count;
    }

    {
        const char* keys = o->bsdUser ? "user,pid,pcpu,pmem,vsz,rss,tty,start,bsdtime,args"
                         : o->full    ? "uid,pid,ppid,c,stime,tty,time,cmd"
                                      : "pid,tty,time,ccomm";
        char copy[128];
        char* items[16];
        int n, i;

        snprintf(copy, sizeof copy, "%s", keys);
        n = split_list(copy, items, 16);
        for (i = 0; i < n; i++) {
            columns[count].field = find_field(items[i]);
            snprintf(columns[count].header, sizeof columns[count].header, "%s", columns[count].field->header);
            count++;
        }
    }
    return count;
}

typedef struct {
    const Clock* clock;
    struct { const Field* field; int descending; } keys[16];
    int          keyCount;
} SortOrder;

static int compare_processes(void* context, const void* left, const void* right) {
    const SortOrder* order = context;
    const Proc* a = *(const Proc* const*)left;
    const Proc* b = *(const Proc* const*)right;
    int k;

    for (k = 0; k < order->keyCount; k++) {
        const Field* f = order->keys[k].field;
        int result;

        if (f->number != NULL) {
            double x = 0, y = 0;
            int haveX = f->number(a, order->clock, &x), haveY = f->number(b, order->clock, &y);

            result = haveX != haveY ? (haveX ? 1 : -1) : (x < y ? -1 : (x > y ? 1 : 0));
        } else {
            char x[MAX_PATH * 4], y[MAX_PATH * 4];

            f->text(a, order->clock, x, sizeof x);
            f->text(b, order->clock, y, sizeof y);
            result = _stricmp(x, y);
        }
        if (result != 0)
            return order->keys[k].descending ? -result : result;
    }
    return a->pid < b->pid ? -1 : (a->pid > b->pid ? 1 : 0);
}

static int parse_sort(const char* text, SortOrder* order) {
    char* copy = _strdup(text);
    char* items[16];
    int n, i;

    if (copy == NULL)
        return 1;
    n = split_list(copy, items, 16);
    for (i = 0; i < n; i++) {
        const char* key = items[i];
        int descending = 0;
        const Field* field;

        if (*key == '-' || *key == '+') {
            descending = *key == '-';
            key++;
        }
        field = find_field(key);
        if (field == NULL)
            return fail("error: unknown sort key", key);
        order->keys[order->keyCount].field = field;
        order->keys[order->keyCount++].descending = descending;
    }
    return 0;
}

static int is_listed(const char* name, char* const* list, int count) {
    int i;

    for (i = 0; i < count; i++)
        if (_stricmp(name, list[i]) == 0)
            return 1;
    return 0;
}

static int command_listed(const Proc* p, char* const* list, int count) {
    char bare[MAX_PATH * 4];
    size_t length;

    snprintf(bare, sizeof bare, "%s", p->name);
    length = strlen(bare);
    if (length > 4 && _stricmp(bare + length - 4, ".exe") == 0)
        bare[length - 4] = '\0';
    return is_listed(p->name, list, count) || is_listed(bare, list, count);
}

static int has_pid(const DWORD* list, int count, DWORD pid) {
    int i;

    for (i = 0; i < count; i++)
        if (list[i] == pid)
            return 1;
    return 0;
}

int main(int argc, char* argv[]) {
    Options options;
    Column columns[LIST_MAX];
    int columnCount, status, procCount = 0, capacity, selectedCount = 0, i, c;
    int needUser = 0, needArguments = 0, hasSelection;
    Proc* procs;
    Proc** chosen;
    Clock clock;
    SortOrder order;
    DWORD consolePids[1024];
    DWORD consoleCount;
    HANDLE snapshot;
    PROCESSENTRY32W entry;
    MEMORYSTATUSEX memory;
    char me[96];
    char** cells;
    int* widths;

    memset(&options, 0, sizeof options);
    memset(&order, 0, sizeof order);
    status = parse_arguments(&options, argc, argv);
    if (status >= 0)
        return status;
    columnCount = build_columns(&options, columns, LIST_MAX);
    if (columnCount < 0)
        return 1;
    if (options.sort[0] != '\0' && parse_sort(options.sort, &order) != 0)
        return 1;

    for (c = 0; c < columnCount; c++) {
        if (strcmp(columns[c].field->key, "user") == 0 || strcmp(columns[c].field->key, "uid") == 0)
            needUser = 1;
        if (columns[c].field->text == text_args)
            needArguments = 1;
    }
    if (options.userCount > 0 || options.mine)
        needUser = 1;
    if (options.sort[0] != '\0') {
        for (i = 0; i < order.keyCount; i++) {
            if (order.keys[i].field->text == text_user)
                needUser = 1;
            if (order.keys[i].field->text == text_args)
                needArguments = 1;
        }
    }

    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return fail("error: cannot list the processes", NULL);
    capacity = 1024;
    procs = calloc((size_t)capacity, sizeof *procs);
    if (procs == NULL)
        return 1;
    entry.dwSize = sizeof entry;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            Proc* p;

            if (procCount == capacity) {
                Proc* bigger;
                capacity *= 2;
                bigger = realloc(procs, (size_t)capacity * sizeof *procs);
                if (bigger == NULL)
                    return 1;
                procs = bigger;
                memset(procs + procCount, 0, (size_t)(capacity - procCount) * sizeof *procs);
            }
            p = &procs[procCount++];
            p->pid = entry.th32ProcessID;
            p->ppid = entry.th32ParentProcessID;
            p->threads = entry.cntThreads;
            utf8_from_wide(entry.szExeFile, -1, p->name, sizeof p->name);
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    GetSystemTimeAsFileTime((FILETIME*)&clock.now);
    memory.dwLength = sizeof memory;
    clock.totalKiB = GlobalMemoryStatusEx(&memory) ? memory.ullTotalPhys / 1024 : 0;
    consoleCount = GetConsoleProcessList(consolePids, 1024);
    account_of(GetCurrentProcess(), me, sizeof me);

    // what is selected: the union of the selections given, the console when there is none
    hasSelection = options.all || options.console || options.mine || options.pidCount > 0 || options.parentCount > 0 ||
                   options.userCount > 0 || options.commandCount > 0;
    if (options.console && options.mine)
        options.all = 1;                        // BSD "ax"
    for (i = 0; i < procCount; i++) {
        Proc* p = &procs[i];
        int pick = 0;

        load_details(p, needUser);
        p->onConsole = has_pid(consolePids, (int)consoleCount, p->pid);
        if (!hasSelection)
            pick = consoleCount > 0 ? p->onConsole : _stricmp(p->user, me) == 0;
        else
            pick = options.all ||
                   (options.console && p->onConsole) ||
                   (options.mine && _stricmp(p->user, me) == 0) ||
                   has_pid(options.pids, options.pidCount, p->pid) ||
                   has_pid(options.parents, options.parentCount, p->ppid) ||
                   is_listed(p->user, options.users, options.userCount) ||
                   command_listed(p, options.commands, options.commandCount);
        p->selected = options.deselect ? !pick : pick;
    }

    chosen = malloc((size_t)(procCount > 0 ? procCount : 1) * sizeof *chosen);
    if (chosen == NULL)
        return 1;
    for (i = 0; i < procCount; i++)
        if (procs[i].selected) {
            if (needArguments) {
                HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, procs[i].pid);
                if (h != NULL) {
                    procs[i].args = command_line_of(h);
                    CloseHandle(h);
                }
            }
            chosen[selectedCount++] = &procs[i];
        }

    order.clock = &clock;
    qsort_s(chosen, (size_t)selectedCount, sizeof *chosen, compare_processes, &order);

    // the cells, then the widths
    cells = calloc((size_t)(selectedCount + 1) * (size_t)columnCount, sizeof *cells);
    widths = calloc((size_t)columnCount, sizeof *widths);
    if (cells == NULL || widths == NULL)
        return 1;
    for (c = 0; c < columnCount; c++) {
        widths[c] = columns[c].field->minimum;
        if ((int)strlen(columns[c].header) > widths[c])
            widths[c] = (int)strlen(columns[c].header);
    }
    for (i = 0; i < selectedCount; i++)
        for (c = 0; c < columnCount; c++) {
            char text[MAX_PATH * 4 + 8];

            columns[c].field->text(chosen[i], &clock, text, sizeof text);
            cells[(size_t)i * columnCount + c] = _strdup(text);
            if (cells[(size_t)i * columnCount + c] == NULL)
                return 1;
            if ((int)strlen(text) > widths[c])
                widths[c] = (int)strlen(text);
        }

    _setmode(_fileno(stdout), _O_BINARY);
    {
        int anyHeader = 0;

        for (c = 0; c < columnCount; c++)
            if (columns[c].header[0] != '\0')
                anyHeader = 1;
        if (!options.noHeaders && anyHeader && selectedCount > 0) {
            for (c = 0; c < columnCount; c++) {
                int last = c == columnCount - 1;
                const char* h = columns[c].header;

                if (columns[c].field->right)
                    printf("%*s", widths[c], h);
                else if (last)
                    fputs(h, stdout);
                else
                    printf("%-*s", widths[c], h);
                if (!last)
                    putchar(' ');
            }
            putchar('\n');
        }
    }
    for (i = 0; i < selectedCount; i++) {
        for (c = 0; c < columnCount; c++) {
            int last = c == columnCount - 1;
            const char* text = cells[(size_t)i * columnCount + c];

            if (columns[c].field->right)
                printf("%*s", widths[c], text);
            else if (last)
                fputs(text, stdout);
            else
                printf("%-*s", widths[c], text);
            if (!last)
                putchar(' ');
        }
        putchar('\n');
    }
    if (fflush(stdout) != 0)
        return 1;
    return selectedCount > 0 ? 0 : 1;
}
