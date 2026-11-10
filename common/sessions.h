#ifndef SESSIONS_H
#define SESSIONS_H

/*
 * The sessions: the terminals that are open. Windows has no utmp file, so Winshell keeps its own record. Every shell that
 * reads from a console registers itself when it starts: it takes the lowest free number N and creates the file pts<N> in
 * the sessions directory (%LOCALAPPDATA%\Winshell\sessions, or the directory named by the environment variable
 * WINSHELL_SESSIONS), keeping it open with the flag "delete on close" - so the record disappears when the shell ends, also
 * when it is killed. The file holds the process id, the start time and the user. who, users, pinky and uptime read these
 * records; a record whose process is gone is not counted.
 *
 * The terminal is called pts/N, as on Linux; the environment variable WINSHELL_PTS of a shell holds the name of its own.
 */

#include <stddef.h>

typedef struct {
    int                number;
    char               tty[16];             /* "pts/0" */
    unsigned long      pid;
    unsigned long long start;               /* FILETIME: 100 ns since 1601-01-01 UTC */
    char               user[256];
} Session;

/** Registers the running process as a session. Returns the number (0, 1, ...), or -1 (no directory, no file). */
int session_open(void);

/** Ends the session of this process (the record is removed). */
void session_close(void);

/** The open sessions, oldest first. The list is allocated (free it). Returns the number of sessions, or -1. */
int session_list(Session** list);

/** The name of the terminal of the shell this process runs in (WINSHELL_PTS), or NULL. */
const char* session_current_tty(void);

/** When the system was started (a FILETIME), from the time it has been running. */
unsigned long long session_boot_time(void);

/** A FILETIME as who shows it, in local time: "Nov 10 12:34". */
void session_time_text(unsigned long long filetime, char* out, size_t size);

#endif //SESSIONS_H
