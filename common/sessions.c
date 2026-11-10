#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "sessions.h"

static HANDLE ownRecord = INVALID_HANDLE_VALUE;

static int sessions_directory(char* out, size_t size) {
    const char* forced = getenv("WINSHELL_SESSIONS");
    char base[MAX_PATH];

    if (forced != NULL && forced[0] != '\0') {
        snprintf(out, size, "%s", forced);
    } else {
        const char* local = getenv("LOCALAPPDATA");

        if (local == NULL || local[0] == '\0')
            return -1;
        snprintf(base, sizeof base, "%s\\Winshell", local);
        CreateDirectoryA(base, NULL);
        snprintf(out, size, "%s\\sessions", base);
    }
    CreateDirectoryA(out, NULL);
    return GetFileAttributesA(out) != INVALID_FILE_ATTRIBUTES ? 0 : -1;
}

static int process_alive(unsigned long pid, unsigned long long recordedStart) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, (DWORD)pid);
    DWORD code;
    FILETIME created, exited, kernel, user;
    int alive = 0;

    if (process == NULL)
        return 0;
    if (GetExitCodeProcess(process, &code) && code == STILL_ACTIVE) {
        alive = 1;
        // the process id may have been given to another process: that one was created after the session began
        if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
            unsigned long long when = ((unsigned long long)created.dwHighDateTime << 32) | created.dwLowDateTime;

            if (when > recordedStart + 20000000ULL)             /* more than two seconds later */
                alive = 0;
        }
    }
    CloseHandle(process);
    return alive;
}

static int read_record(const char* path, Session* out) {
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    char text[1024];
    DWORD got = 0;
    char* line;
    int haveStart = 0;

    if (file == INVALID_HANDLE_VALUE)
        return -1;
    if (!ReadFile(file, text, sizeof text - 1, &got, NULL))
        got = 0;
    CloseHandle(file);
    text[got] = '\0';
    memset(out, 0, sizeof *out);
    out->pid = 0;
    for (line = strtok(text, "\n"); line != NULL; line = strtok(NULL, "\n")) {
        size_t n = strlen(line);

        if (n > 0 && line[n - 1] == '\r')
            line[n - 1] = '\0';
        if (strncmp(line, "pid=", 4) == 0)
            out->pid = strtoul(line + 4, NULL, 10);
        else if (strncmp(line, "start=", 6) == 0) {
            out->start = strtoull(line + 6, NULL, 10);
            haveStart = 1;
        } else if (strncmp(line, "user=", 5) == 0)
            snprintf(out->user, sizeof out->user, "%s", line + 5);
    }
    return out->pid != 0 && haveStart ? 0 : -1;
}

int session_open(void) {
    char directory[MAX_PATH], path[MAX_PATH + 16];
    int number;
    char user[256] = "";
    DWORD userSize = sizeof user;
    FILETIME now;
    char text[512];
    DWORD written;

    if (ownRecord != INVALID_HANDLE_VALUE)
        return -1;
    if (sessions_directory(directory, sizeof directory) != 0)
        return -1;
    if (!GetUserNameA(user, &userSize))
        snprintf(user, sizeof user, "?");
    for (number = 0; number < 1000; number++) {
        HANDLE file;

        snprintf(path, sizeof path, "%s\\pts%d", directory, number);
        file = CreateFileA(path, GENERIC_WRITE | DELETE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_NEW,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE, NULL);
        if (file == INVALID_HANDLE_VALUE) {
            // taken: if its shell is gone, the record is left over and can be replaced
            Session other;

            if (read_record(path, &other) == 0 && !process_alive(other.pid, other.start)) {
                if (DeleteFileA(path)) {
                    number--;
                    continue;
                }
            }
            continue;
        }
        GetSystemTimeAsFileTime(&now);
        snprintf(text, sizeof text, "pid=%lu\nstart=%llu\nuser=%s\n", (unsigned long)GetCurrentProcessId(),
                 ((unsigned long long)now.dwHighDateTime << 32) | now.dwLowDateTime, user);
        WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
        ownRecord = file;
        return number;
    }
    return -1;
}

void session_close(void) {
    if (ownRecord != INVALID_HANDLE_VALUE) {
        CloseHandle(ownRecord);                 /* the file goes with the last handle */
        ownRecord = INVALID_HANDLE_VALUE;
    }
}

static int compare_sessions(const void* pa, const void* pb) {
    const Session* a = (const Session*)pa;
    const Session* b = (const Session*)pb;

    if (a->start != b->start)
        return a->start < b->start ? -1 : 1;
    return a->number - b->number;
}

int session_list(Session** list) {
    char directory[MAX_PATH], pattern[MAX_PATH + 8];
    WIN32_FIND_DATAA data;
    HANDLE find;
    Session* items = NULL;
    size_t count = 0, capacity = 0;

    *list = NULL;
    if (sessions_directory(directory, sizeof directory) != 0)
        return 0;
    snprintf(pattern, sizeof pattern, "%s\\pts*", directory);
    find = FindFirstFileA(pattern, &data);
    if (find == INVALID_HANDLE_VALUE)
        return 0;
    do {
        Session s;
        char path[MAX_PATH * 2];
        char* end;
        long number = strtol(data.cFileName + 3, &end, 10);

        if (*end != '\0' || end == data.cFileName + 3)
            continue;
        snprintf(path, sizeof path, "%s\\%s", directory, data.cFileName);
        if (read_record(path, &s) != 0 || !process_alive(s.pid, s.start))
            continue;
        s.number = (int)number;
        snprintf(s.tty, sizeof s.tty, "pts/%d", s.number);
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 8;
            items = (Session*)realloc(items, capacity * sizeof(Session));
            if (items == NULL)
                break;
        }
        items[count++] = s;
    } while (FindNextFileA(find, &data));
    FindClose(find);
    if (items != NULL)
        qsort(items, count, sizeof(Session), compare_sessions);
    *list = items;
    return (int)count;
}

const char* session_current_tty(void) {
    const char* tty = getenv("WINSHELL_PTS");

    return tty != NULL && tty[0] != '\0' ? tty : NULL;
}

unsigned long long session_boot_time(void) {
    FILETIME now;
    unsigned long long value;

    GetSystemTimeAsFileTime(&now);
    value = ((unsigned long long)now.dwHighDateTime << 32) | now.dwLowDateTime;
    return value - GetTickCount64() * 10000ULL;
}

void session_time_text(unsigned long long filetime, char* out, size_t size) {
    static const char* const months[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    FILETIME utc, local;
    SYSTEMTIME t;

    utc.dwLowDateTime = (DWORD)(filetime & 0xffffffffULL);
    utc.dwHighDateTime = (DWORD)(filetime >> 32);
    if (!FileTimeToLocalFileTime(&utc, &local) || !FileTimeToSystemTime(&local, &t)) {
        snprintf(out, size, "?");
        return;
    }
    snprintf(out, size, "%s %2d %02d:%02d", months[t.wMonth - 1], t.wDay, t.wHour, t.wMinute);
}
