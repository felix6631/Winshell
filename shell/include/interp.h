#ifndef INTERP_H
#define INTERP_H

/**
 * Running lines of commands: what the input loop of the shell does with one line, and what a script,
 * `source` and `shell -c` do with many. Also command substitution, which runs a line and gives back
 * what it wrote to standard output.
 */

/**
 * Runs one line (a list of pipelines joined by ; && ||): each pipeline is parsed when its turn has
 * come, so it sees the directory and the variables the one before left. status is the status of the
 * command before, which a lone `exit` keeps and `&&` and `||` look at. Returns the status of the last
 * command that ran. *exitShell is set when the shell has to end (`exit`).
 */
int interp_run_line(const char* line, int* exitShell, int status);

/**
 * Runs the lines of a file, one after the other, until its end or an `exit`. A line that cannot be
 * parsed is reported and the next one runs. #! and # lines are comments.
 *
 * With setParameters the file is a script run with arguments: $0 is its name, $1.. $9 the first
 * arguments, $# their number, $@ and $* all of them; the parameters of the caller come back
 * afterwards. Without it (source FILE) the parameters are the caller's.
 * Returns the status of the last command, 127 when the file cannot be read.
 */
int interp_run_file(const char* path, const char* const* arguments, int argumentCount, int setParameters,
                    int* exitShell, int status);

/** Makes $(...) work in the parser: call once at the start. */
void interp_init(void);

/**
 * How many substitutions have been run, and the status of the last one: NAME=$(command) alone has
 * the status of the command, as in other shells.
 */
unsigned long interp_substitution_count(void);
int interp_substitution_status(void);

#endif //INTERP_H
