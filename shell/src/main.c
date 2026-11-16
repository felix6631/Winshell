#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "parser.h"
#include "interp.h"
#include "command.h"
#include "cwd.h"
#include "jobs.h"
#include "variables.h"
#include "vec.h"
#include "sessions.h"

/** Set when Ctrl+C or Ctrl+Break reached the shell. */
static volatile LONG interrupted = 0;

/**
 * Ctrl+C and Ctrl+Break are delivered to every process on the console.
 * The shell swallows them so that it survives, while the program it is
 * running has its own default handler and is terminated.
 * Closing the console, logging off and shutting down are left to the default handler.
 *
 * SetConsoleCtrlHandler(NULL, TRUE) is deliberately not used: that setting
 * would be inherited, and the programs started by the shell could not be interrupted.
 */
static BOOL WINAPI winshell_ctrlHandler(DWORD type) {
    switch (type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            InterlockedExchange(&interrupted, 1);
            command_interrupt();
            return TRUE;
        default:
            return FALSE;
    }
}

/* the code pages of the console before the shell changed them */
static UINT savedInputCodePage, savedOutputCodePage;

static void restore_console_code_pages(void) {
    if (savedInputCodePage != 0)
        SetConsoleCP(savedInputCodePage);
    if (savedOutputCodePage != 0)
        SetConsoleOutputCP(savedOutputCodePage);
}

/**
 * The programs of the shell write UTF-8 (see common/utf8.manifest), and so do the lines it reads: the
 * console is put into UTF-8 for as long as the shell runs, and given back at the end.
 */
static void use_utf8_console(void) {
    DWORD mode;

    if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) || GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &mode)) {
        savedInputCodePage = GetConsoleCP();
        savedOutputCodePage = GetConsoleOutputCP();
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
        atexit(restore_console_code_pages);
    }
}

static void winshell_terminalInit(void) {
    SetConsoleCtrlHandler(winshell_ctrlHandler, TRUE);
    use_utf8_console();
    cwd_init();
}

/**
 * One line typed at the console, as UTF-8 with its line ending. The wide function is used so that
 * what is typed (an IME composes Korean, for one) arrives whole whatever the code page is.
 * Returns NULL at the end of input (Ctrl+Z) and when Ctrl+C interrupted the reading.
 */
static char* read_console_line(HANDLE in) {
    wchar_t* wide = NULL;
    size_t length = 0, capacity = 0;
    char* line;
    int bytes;

    for (;;) {
        wchar_t chunk[512];
        DWORD got = 0;

        if (!ReadConsoleW(in, chunk, 512, &got, NULL) || got == 0) {
            free(wide);
            return NULL;
        }
        if (VEC_RESERVE(wide, capacity, length + got + 1) != 0) {
            free(wide);
            return NULL;
        }
        memcpy(wide + length, chunk, got * sizeof *chunk);
        length += got;
        if (chunk[got - 1] == L'\n')
            break;
    }

    // Ctrl+Z at the start of a line is the end of the input
    if (wide[0] == 0x1A) {
        free(wide);
        return NULL;
    }

    bytes = WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, NULL, 0, NULL, NULL);
    line = malloc((size_t)bytes + 1);
    if (line != NULL) {
        WideCharToMultiByte(CP_UTF8, 0, wide, (int)length, line, bytes, NULL, NULL);
        line[bytes] = '\0';
    }
    free(wide);
    return line;
}

/**
 * Reads one whole line from stdin, however long it is.
 * Returns NULL at the end of input, or when reading was interrupted.
 * The result has to be freed.
 */
static char* read_line(void) {
    size_t length = 0, capacity = BUFSIZ;
    char* line;
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (in != INVALID_HANDLE_VALUE && GetConsoleMode(in, &mode))
        return read_console_line(in);

    line = malloc(capacity);
    if (line == NULL)
        return NULL;

    for (;;) {
        if (fgets(line + length, (int)(capacity - length), stdin) == NULL) {
            if (length == 0 || interrupted) {
                free(line);
                return NULL;
            }
            break;
        }

        {
            size_t got = strlen(line + length);
            if (got == 0)
                continue;       // a NUL byte: nothing to keep, and there is no last character to look at
            length += got;
        }
        if (line[length - 1] == '\n')
            break;

        // buffer is full without a line feed; make room for the rest
        if (length + 1 >= capacity && VEC_RESERVE(line, capacity, capacity + 1) != 0) {
            free(line);
            return NULL;
        }
    }
    return line;
}

static void print_usage(void) {
    puts("Usage: shell                         read commands from the terminal or from standard input\n"
         "       shell FILE [ARGUMENT]...      run the commands of FILE; $0 is FILE, $1.. its arguments\n"
         "       shell -c COMMAND [NAME [ARGUMENT]...]   run COMMAND ($0 is NAME)\n"
         "       shell -s [ARGUMENT]...        read the commands from standard input; the arguments are $1..\n"
         "\n"
         "  Options are read as bash reads them and may be written together (-ec, -lc, -euo pipefail):\n"
         "  -c -s -e -u -o NAME (errexit nounset pipefail), --login --noprofile --norc --posix and -- are\n"
         "  accepted; -l -i -x -v -f -h -m -B -H and the other letters are accepted and do nothing.\n"
         "\n"
         "  --help      display this help and exit\n"
         "  --version   output version information and exit\n"
         "\n"
         "Inside a script `source FILE [ARGUMENT]...` runs another file in the same shell. $(command) and\n"
         "`command` are replaced by the output of the command, # starts a comment.");
}

/** Runs the commands of the terminal or of standard input, one line at a time. */
static int interactive_loop(void) {
    int status = 0, exitShell = 0;
    DWORD mode;

    int prompting = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) != 0;

    jobs_set_interactive(prompting);
    while (!exitShell) {
        char* input;

        jobs_report_finished();
        if (prompting) {
            printf(" > ");
            fflush(stdout);
        }

        InterlockedExchange(&interrupted, 0);
        input = read_line();
        if (input == NULL) {
            // Ctrl+C at the prompt abandons the line, it does not end the shell
            if (interrupted) {
                InterlockedExchange(&interrupted, 0);
                clearerr(stdin);
                putchar('\n');
                continue;
            }
            // end of input
            break;
        }

        // strip linefeed
        input[strcspn(input, "\r\n")] = '\0';

        // a command that is not finished (an `if` without its `fi`, an open quote) goes on in the next lines
        while (command_incomplete(input)) {
            DWORD mode;
            char* more;
            char* joined;

            if (prompting) {
                printf("> ");
                fflush(stdout);
            }
            more = read_line();
            if (more == NULL)
                break;                  // the end of the input: what is there is run, and reported
            more[strcspn(more, "\r\n")] = '\0';
            joined = malloc(strlen(input) + strlen(more) + 2);
            if (joined == NULL) {
                free(more);
                break;
            }
            sprintf(joined, "%s\n%s", input, more);
            free(input);
            free(more);
            input = joined;
        }

        status = interp_run_line(input, &exitShell, status);
        free(input);
    }
    return status;
}

/** What the command line asked for. */
typedef struct {
    int         commandMode;        // -c: the first operand is the commands
    int         stdinMode;          // -s: the commands come from standard input, the operands are the parameters
    int         errexit;            // -e
    int         nounset;            // -u
    int         pipefail;           // -o pipefail
    int         bad;                // a message was printed
    int         version, help;
    int         first;              // the index of the first operand
} Options;

static void set_option_word(const char* word, int on, Options* o) {
    if (strcmp(word, "errexit") == 0)
        o->errexit = on;
    else if (strcmp(word, "nounset") == 0)
        o->nounset = on;
    else if (strcmp(word, "pipefail") == 0)
        o->pipefail = on;
    // the other options of bash (xtrace, posix, noclobber, ...) are accepted and do nothing
}

/**
 * The options in front of the operands, as bash reads them: single letters that can be written together (-ec, -lc,
 * -euo pipefail), -o NAME, --login --noprofile --norc --posix --help --version, and -- to end them.
 */
static void parse_options(int argc, char* argv[], Options* o) {
    int i;

    memset(o, 0, sizeof *o);
    for (i = 1; i < argc; i++) {
        const char* a = argv[i];
        int on;

        if (strcmp(a, "--") == 0) {
            i++;
            break;
        }
        if (strcmp(a, "-") == 0)
            break;
        if (a[0] != '-' && a[0] != '+')
            break;
        if (strcmp(a, "--help") == 0) {
            o->help = 1;
            continue;
        }
        if (strcmp(a, "--version") == 0) {
            o->version = 1;
            continue;
        }
        if (strcmp(a, "--login") == 0 || strcmp(a, "--noprofile") == 0 || strcmp(a, "--norc") == 0 || strcmp(a, "--posix") == 0 ||
            strcmp(a, "--noediting") == 0 || strcmp(a, "--restricted") == 0 || strcmp(a, "--verbose") == 0)
            continue;
        if (a[0] == '-' && a[1] == '-') {
            fprintf(stderr, "shell: %s: invalid option\n", a);
            o->bad = 1;
            return;
        }
        on = a[0] == '-';
        for (a++; *a != '\0'; a++) {
            switch (*a) {
            case 'c': o->commandMode = on; break;
            case 's': o->stdinMode = on; break;
            case 'e': o->errexit = on; break;
            case 'u': o->nounset = on; break;
            case 'o':
                if (i + 1 < argc) {
                    set_option_word(argv[++i], on, o);
                } else {
                    fprintf(stderr, "shell: -o: option requires an argument\n");
                    o->bad = 1;
                    return;
                }
                break;
            case 'l': case 'i': case 'x': case 'v': case 'f': case 'h': case 'm': case 'B': case 'H': case 'n': case 'a':
            case 'b': case 'k': case 'p': case 'r': case 't': case 'C': case 'E': case 'P': case 'T':
                break;
            default:
                fprintf(stderr, "shell: -%c: invalid option\n", *a);
                o->bad = 1;
                return;
            }
        }
    }
    o->first = i;
}

/** Applies -e, -u and -o pipefail through the builtin, so that the options mean what `set` means. */
static void apply_options(const Options* o) {
    char* errexit[] = { "set", "-e", NULL };
    char* nounset[] = { "set", "-u", NULL };
    char* pipefail[] = { "set", "-o", "pipefail", NULL };

    if (o->errexit)
        command_set(errexit);
    if (o->nounset)
        command_set(nounset);
    if (o->pipefail)
        command_set(pipefail);
}

int main(int argc, char* argv[]) {
    int status = 0, exitShell = 0;
    Options o;
    int operandCount;
    char** operands;

    parse_options(argc, argv, &o);
    if (o.help) {
        print_usage();
        return 0;
    }
    if (o.version) {
        puts("shell 0.1");
        return 0;
    }
    if (o.bad)
        return 2;
    operands = argv + o.first;
    operandCount = argc - o.first;

    winshell_terminalInit();
    interp_init();
    interp_set_line_reader(read_line);
    apply_options(&o);

    if (o.commandMode) {
        // shell -c COMMAND [NAME [ARGUMENT]...]
        const char* name;
        const char* const* arguments;
        int count;

        if (operandCount < 1) {
            fprintf(stderr, "shell: -c: option requires an argument\n");
            return 2;
        }
        name = operandCount >= 2 ? operands[1] : "shell";
        arguments = (const char* const*)(operandCount >= 3 ? operands + 2 : operands + operandCount);
        count = operandCount >= 3 ? operandCount - 2 : 0;
        interp_set_parameters(name, arguments, count);
        status = interp_run_line(operands[0], &exitShell, 0);
    } else if (operandCount >= 1 && !o.stdinMode) {
        // shell FILE [ARGUMENT]...
        interp_mainScript = 1;
        status = interp_run_file(operands[0], (const char* const*)operands + 1, operandCount - 1, 1, &exitShell, 0);
    } else {
        DWORD consoleMode;
        int console = GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &consoleMode);

        if (o.stdinMode && operandCount > 0)
            interp_set_parameters("shell", (const char* const*)operands, operandCount);
        // a shell that reads from a console is a session: who and the others list it (the record goes with the process)
        if (console || getenv("WINSHELL_TEST_SESSION") != NULL) {
            int number = session_open();

            if (number >= 0) {
                char tty[16];

                snprintf(tty, sizeof tty, "pts/%d", number);
                var_export("WINSHELL_PTS", tty);
            }
        }
        status = interactive_loop();
        session_close();
    }
    command_run_exit_trap(&status);
    fflush(stdout);
    return status;
}
