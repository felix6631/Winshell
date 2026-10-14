#ifndef COMMAND_H
#define COMMAND_H

/**
 * The commands of a script above the single pipeline: lists joined by ; && || and newlines, and the
 * compound commands
 *
 *     if LIST; then LIST; [elif LIST; then LIST;]... [else LIST;] fi
 *     while LIST; do LIST; done        until LIST; do LIST; done
 *     for NAME [in WORD...]; do LIST; done
 *
 * with break and continue. A reserved word is one only as the first word of a command, unquoted; the
 * rest of a command is what the pipeline parser (parser.h) reads. A compound command stands where a
 * command stands in a list (`if ...; fi && echo`), but not inside a pipeline. Comments start with # at
 * the start of a word.
 *
 * Text is parsed and run one command of the top level at a time, so what is before a syntax error has
 * run, and a command sees what the one before it did.
 */

/**
 * Runs the commands of the text (any number of lines). status is that of the command before: `exit`
 * without a number keeps it, and && and || look at it. Returns the status of the last command that ran
 * (2 for a syntax error, which ends the text). *exitShell is set by `exit`.
 */
int command_run_text(const char* text, int* exitShell, int status);

/** Whether text can be run as it is: 0 complete (or a syntax error, which waiting would not cure), 1 when more input is needed. */
int command_incomplete(const char* text);

/**
 * break [N] and continue [N] (builtins): leave N enclosing loops, or go to the next round of the Nth.
 * Returns 0, or 1 after a message when no loop is running.
 */
int command_break(int levels, int isContinue);

/** Ctrl+C: the loops that are running stop (status 130) at the end of the round. Called from the console handler. */
void command_interrupt(void);

#endif //COMMAND_H
