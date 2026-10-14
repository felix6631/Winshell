#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "parser.h"
#include "interp.h"
#include "command.h"
#include "cwd.h"
#include "variables.h"
#include "vec.h"

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

    while (!exitShell) {
        char* input;

        printf(" > ");
        fflush(stdout);

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

            if (GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode)) {
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

int main(int argc, char* argv[]) {
    int status = 0, exitShell = 0;

    if (argc >= 2 && strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (argc >= 2 && strcmp(argv[1], "--version") == 0) {
        puts("shell 0.1");
        return 0;
    }

    winshell_terminalInit();
    interp_init();
    interp_set_line_reader(read_line);

    if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
        // shell -c COMMAND [NAME [ARGUMENT]...]
        const char* name = argc >= 4 ? argv[3] : "shell";
        const char* const* arguments = (const char* const*)(argc >= 5 ? argv + 4 : argv + argc);
        int count = argc >= 5 ? argc - 4 : 0;
        char number[16];
        char* joined = NULL;
        int i;

        var_set("0", name);
        for (i = 0; i < count && i < 9; i++) {
            snprintf(number, sizeof number, "%d", i + 1);
            var_set(number, arguments[i]);
        }
        snprintf(number, sizeof number, "%d", count);
        var_set("#", number);
        if (count > 0) {
            size_t total = 1;
            for (i = 0; i < count; i++)
                total += strlen(arguments[i]) + 1;
            joined = malloc(total);
            if (joined != NULL) {
                size_t used = 0;
                for (i = 0; i < count; i++)
                    used += (size_t)sprintf(joined + used, "%s%s", i > 0 ? " " : "", arguments[i]);
                var_set("@", joined);
                var_set("*", joined);
                free(joined);
            }
        }
        status = interp_run_line(argv[2], &exitShell, 0);
    } else if (argc >= 2 && strcmp(argv[1], "-c") != 0 && argv[1][0] != '-') {
        // shell FILE [ARGUMENT]...
        status = interp_run_file(argv[1], (const char* const*)argv + 2, argc - 2, 1, &exitShell, 0);
    } else if (argc >= 2) {
        fprintf(stderr, "shell: %s: invalid option\n", argv[1]);
        return 2;
    } else {
        status = interactive_loop();
    }
    fflush(stdout);
    return status;
}
