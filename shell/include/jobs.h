#ifndef JOBS_H
#define JOBS_H

#include <stddef.h>
#include <windows.h>

/**
 * Background jobs (`command &`). A job is one or more processes the shell did not wait for; the table
 * numbers them 1, 2, ... and keeps them until their end has been reported or waited for.
 */

/** Whether the shell is used from a terminal: a job is announced with `[1] 4242` and its end is reported. */
void jobs_set_interactive(int on);

/**
 * Adds a job of the processes (the table owns the array and the handles). The last process is $!.
 * Returns the number of the job.
 */
int jobs_add(HANDLE* processes, size_t count, const char* label);

/**
 * Starts `argv` (argv[0] the program, the rest its arguments) as a job with standard input NUL and its
 * own process group (Ctrl+C does not reach it). Returns the number of the job, or -1 after a message.
 */
int jobs_start_shell(char* const argv[], const char* label);

/** The builtin jobs. */
int jobs_builtin_jobs(char* const* argv);

/** The builtin wait [ID...]: ID is a process id or %N. Returns the status of the last one waited for. */
int jobs_builtin_wait(char* const* argv);

/**
 * Job control. A job is named %N (or N), %% / %+ (the current one: the last started), %- (the one before),
 * %string (its command starts with it) or %?string (has it). fg waits for the job and gives its status (Ctrl+C
 * ends it); bg lets a stopped job (kill -STOP) go on, and says so when it runs already; disown forgets jobs
 * (-a all of them, no argument the current one) without ending them.
 */
int jobs_builtin_fg(char* const* argv);
int jobs_builtin_bg(char* const* argv);
int jobs_builtin_disown(char* const* argv);

/**
 * kill %JOB...: the arguments that are job specs are replaced by the process ids of the job. jobs_has_job_argument
 * tells whether there is one; jobs_expand_kill_arguments returns the new argv (NULL after a message); free it with
 * jobs_free_arguments (count (size_t)-1: up to the NULL).
 */
int jobs_has_job_argument(char* const* argv);
char** jobs_expand_kill_arguments(char* const* argv);
void jobs_free_arguments(char** arguments, size_t count);

/** Prints the jobs that have ended since the last time (`[1]+  Done   command`) and forgets them. */
void jobs_report_finished(void);

#endif //JOBS_H
