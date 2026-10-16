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

/**
 * Functions: `name() compound-command` defines one (for the rest of the session); a command whose name is a
 * function runs its body with the arguments as $1.. $9, $# and $@. command_has_function tells whether a
 * name is one, command_call_function runs it (argv[0] is the name; 127 when there is none).
 * return [N] ends the function with the status N (default: that of the last command); local NAME[=value]
 * makes the variable the function's own, put back when it returns. Both return 0, or 1 after a message
 * when not in a function. command_unset_function removes one (unset -f); returns 1 when there was one.
 */
int command_has_function(const char* name);
int command_call_function(const char* name, char* const* argv, int* exitShell, int status);
int command_return(int status);
int command_local(const char* name, const char* value);
int command_unset_function(const char* name);

/**
 * trap [-lp] [ACTION] SIGNAL...: the builtin (argv[0] is "trap"). The signals are EXIT (0), INT (2, Ctrl+C) and
 * ERR (a command failed); ACTION is run as commands, "" ignores it, - resets it. Returns the status.
 * command_run_exit_trap runs the EXIT trap when the shell ends (an `exit N` inside it sets *status).
 * A subshell or a substitution starts without the traps: enter returns what to give back to leave.
 */
int command_trap(char* const* argv);
void command_run_exit_trap(int* status);
void* command_traps_enter_subshell(void);
void command_traps_leave_subshell(void* saved);

/** Whether a Ctrl+C is waiting to be served (the builtin wait stops then). */
int command_was_interrupted(void);

/** Ctrl+C: the loops that are running stop (status 130) at the end of the round. Called from the console handler. */
void command_interrupt(void);

#endif //COMMAND_H
