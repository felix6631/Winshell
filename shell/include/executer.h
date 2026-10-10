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
 * the next one. Redirections of a command take precedence over the pipe.
 * Without any, standard input, output and error are those of the shell.
 *
 * A command that cannot be started, or a redirection whose file cannot be
 * opened, is reported on standard error. The rest of the pipeline still runs;
 * the neighbours of the failed command see an empty input or a closed output.
 *
 * It returns the exit code of the last command, or -1 when that command
 * could not be started.
 */
int execute_pipeline(const Pipeline* pipeline);

#endif //EXECUTER_H
