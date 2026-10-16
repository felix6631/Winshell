#ifndef EXECUTER_H
#define EXECUTER_H

#include "parser.h"

/**
 * execute_pipeline runs every command of the pipeline and waits for all of
 * them to exit.
 *
 * Programs are looked up in the directory of the shell (the project's bin
 * directory); ".exe" is appended when missing. All commands run at the same
 * time, the standard output of each being connected to the standard input of
 * the next one. Redirections of a command take precedence over the pipe, and
 * those of standard output and error are applied in the order written, so
 * a copy (2>&1) is of what the descriptor was at that point.
 * Without any, standard input, output and error are those of the shell.
 * The NAME=value words of a command are added to its environment only.
 * A command that is only NAME=value starts no program; its redirections are made.
 *
 * A command that cannot be started, or a redirection whose file cannot be
 * opened, is reported on standard error. The rest of the pipeline still runs;
 * the neighbours of the failed command see an empty input or a closed output.
 *
 * It returns the exit code of the last command, or -1 when that command
 * could not be started.
 */
int execute_pipeline(const Pipeline* pipeline);

/**
 * The same without waiting (`command &`): the standard input is NUL and the processes are in a group of their own.
 * Returns 0 and the processes that were started (malloc'ed array, handles for the caller to keep), or -1 when
 * none could be started.
 */
int execute_pipeline_background(const Pipeline* pipeline, HANDLE** processes, size_t* count);

/**
 * The redirections of a compound command (`done < file`, `fi > out 2>&1`): they are applied to the standard
 * descriptors of the shell itself for as long as the command runs, so that what it runs (builtins such as
 * read, and the programs it starts) writes and reads there, and restored after it. The command is the one
 * the parser made of "x <redirections>"; its words are not used.
 */
typedef struct {
    int saved[3];           // the descriptors 0, 1, 2 as they were, or -1 when not changed
    int inputRedirected;    // standard input was replaced
} StdRedirection;

/** Applies the redirections in the order written. Returns 0, or -1 after a message (nothing is changed then). */
int redirect_begin(const Command* command, StdRedirection* saved);

/** Puts the descriptors back. */
void redirect_end(StdRedirection* saved);

#endif //EXECUTER_H
