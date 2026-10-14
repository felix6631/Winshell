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
 * Runs one simple command (a pipeline, NAME=value, or a builtin): variables, substitutions and wildcards
 * are expanded, then it runs. The compound commands of command.h call this for each command.
 */
int interp_run_simple(const char* text, int* exitShell, int status);

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

/**
 * Runs the text with its standard output collected (in a subshell: the directory and the variables are as
 * they were afterwards). *output is the text, NUL ended (to be freed), *length its size, *status the
 * status of the last command. Returns 0, or -1 when out of memory.
 */
int interp_capture(const char* command, char** output, size_t* length, int* status);

/**
 * Input for `read`: one line from the standard input of the shell, without its line end. Returns 1, or 0 at
 * the end of the input. The line (to be freed) comes from the function given to interp_set_line_reader
 * (the terminal or the script on standard input) unless standard input was replaced for a compound
 * command (interp_input_redirected), then from descriptor 0.
 */
int interp_read_line(char** line);
void interp_set_line_reader(char* (*reader)(void));
void interp_input_redirected(int delta);

/** Feeds the bytes to the standard input of the shell (and of what it starts) until the restore. */
typedef struct InputRedirection InputRedirection;
InputRedirection* interp_input_from_bytes(const char* data, size_t length);
void interp_input_restore(InputRedirection* input);

/**
 * The positional parameters of a function call: $1.. $9, $#, $@ and $* become the arguments until the
 * frame is popped, which puts the caller's back ($0 is not changed). Returns NULL when out of memory.
 */
typedef struct ParameterFrame ParameterFrame;
ParameterFrame* interp_parameters_push(const char* const* arguments, int count);
void interp_parameters_pop(ParameterFrame* frame);

/** Makes $(...) work in the parser: call once at the start. */
void interp_init(void);

/**
 * How many substitutions have been run, and the status of the last one: NAME=$(command) alone has
 * the status of the command, as in other shells.
 */
unsigned long interp_substitution_count(void);
int interp_substitution_status(void);

#endif //INTERP_H
