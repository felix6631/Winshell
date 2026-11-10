/*
 * stty: prints or changes the settings of a terminal. A Windows console has no termios; what it has is the input mode
 * (line input, echo, processing of Ctrl+C) and the size, so these are what stty can show and set:
 *   size                 rows and columns of the console window
 *   (no argument), -a    the size and isig, icanon and echo
 *   -g                   a string for stty to restore the settings with ("winshell:INPUT:OUTPUT")
 *   isig icanon echo     and their negations; raw, cooked, cbreak and sane are combinations of them
 * Every other setting of GNU stty is checked as GNU checks it (names, missing arguments, the exclusive output styles)
 * and then refused, because the console cannot do it. The exit status is 1 for any error.
 */

#include <ctype.h>
#include <fcntl.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "fsutil.h"
#include "vpath.h"

#define VERSION "0.1"
#define PROGRAM "stty"

static void print_usage(void) {
    puts("Usage: stty [-F DEVICE | --file=DEVICE] [SETTING]...\n"
         "  or:  stty [-F DEVICE | --file=DEVICE] [-a|--all]\n"
         "  or:  stty [-F DEVICE | --file=DEVICE] [-g|--save]\n"
         "Print or change terminal characteristics.\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short options too.\n"
         "  -a, --all          print all current settings in human-readable form\n"
         "  -g, --save         print all current settings in a stty-readable form\n"
         "  -F, --file=DEVICE  open and use the specified DEVICE instead of stdin\n"
         "      --help     display this help and exit\n"
         "      --version  output version information and exit\n"
         "\n"
         "Optional - before SETTING indicates negation.\n"
         "\n"
         "A Windows console has no termios. These settings work: size, isig, icanon, echo (and their\n"
         "negations), raw, cooked, cbreak and sane. All the other settings of GNU stty are accepted as\n"
         "arguments and then refused. Without a setting the size and these modes are printed.");
}

/* ---- the settings GNU stty knows ------------------------------------------------------------ */

static const char* const flags[] = {
    "parenb", "parodd", "cmspar", "hupcl", "hup", "cstopb", "cread", "clocal", "crtscts", "ignbrk", "brkint",
    "ignpar", "parmrk", "inpck", "istrip", "inlcr", "igncr", "icrnl", "ixon", "ixoff", "tandem", "ixany", "imaxbel",
    "iuclc", "opost", "olcuc", "ocrnl", "onlcr", "onocr", "onlret", "ofill", "ofdel", "isig", "icanon", "iexten",
    "echo", "echoe", "echok", "echonl", "noflsh", "xcase", "tostop", "echoprt", "prterase", "echoctl", "ctlecho",
    "echoke", "crterase", "flusho", "extproc", "evenp", "parity", "oddp", "nl", "raw", "cooked", "cbreak", "pass8",
    "litout", "decctlq", "tabs", "lcase", "LCASE", "crt", "dec", "cs5", "cs6", "cs7", "cs8", "nl0", "nl1", "cr0", "cr1",
    "cr2", "cr3", "tab0", "tab1", "tab2", "tab3", "bs0", "bs1", "vt0", "vt1", "ff0", "ff1", "tabs", NULL
};

// settings that cannot be negated
static const char* const plain[] = { "sane", "ek", "size", "speed", NULL };

// settings with a value
static const char* const valued[] = {
    "intr", "quit", "erase", "kill", "eof", "eol", "eol2", "swtch", "start", "stop", "susp", "rprnt", "werase", "lnext",
    "discard", "min", "time", "ispeed", "ospeed", "cols", "columns", "rows", "line", NULL
};

static int listed(const char* const* list, const char* name) {
    int i;

    for (i = 0; list[i] != NULL; i++)
        if (strcmp(list[i], name) == 0)
            return 1;
    return 0;
}

static int is_speed(const char* text) {
    static const char* const speeds[] = {
        "0", "50", "75", "110", "134", "134.5", "150", "200", "300", "600", "1200", "1800", "2400", "4800", "9600",
        "19200", "38400", "57600", "115200", "230400", "460800", "500000", "576000", "921600", "1000000", "1152000",
        "1500000", "2000000", "2500000", "3000000", "3500000", "4000000", NULL
    };

    return listed(speeds, text);
}

static int is_saved_form(const char* text) {
    return strncmp(text, "winshell:", 9) == 0;
}

/* ---- the console ------------------------------------------------------------------------------- */

static HANDLE input;

static int get_size(int* rows, int* columns) {
    HANDLE out = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    CONSOLE_SCREEN_BUFFER_INFO info;
    int ok;

    if (out == INVALID_HANDLE_VALUE)
        return 0;
    ok = GetConsoleScreenBufferInfo(out, &info) != 0;
    CloseHandle(out);
    if (ok) {
        *rows = info.srWindow.Bottom - info.srWindow.Top + 1;
        *columns = info.srWindow.Right - info.srWindow.Left + 1;
    }
    return ok;
}

static DWORD output_mode(void) {
    HANDLE out = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD mode = 0;

    if (out != INVALID_HANDLE_VALUE) {
        GetConsoleMode(out, &mode);
        CloseHandle(out);
    }
    return mode;
}

static void set_output_mode(DWORD mode) {
    HANDLE out = CreateFileA("CONOUT$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

    if (out != INVALID_HANDLE_VALUE) {
        SetConsoleMode(out, mode);
        CloseHandle(out);
    }
}

static void print_modes(DWORD mode) {
    printf("%sisig %sicanon %secho\n", (mode & ENABLE_PROCESSED_INPUT) ? "" : "-", (mode & ENABLE_LINE_INPUT) ? "" : "-",
           (mode & ENABLE_ECHO_INPUT) ? "" : "-");
}

static void invalid_argument(const char* text) {
    fprintf(stderr, PROGRAM ": invalid argument '%s'\nTry '" PROGRAM " --help' for more information.\n", text);
}

/** Applies one setting to the mode of the input. Returns 0, or 1 after a message. */
static int apply(const char* name, int negated, DWORD* mode) {
    DWORD all = ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
    DWORD bit = 0;

    if (strcmp(name, "isig") == 0)
        bit = ENABLE_PROCESSED_INPUT;
    else if (strcmp(name, "icanon") == 0)
        bit = ENABLE_LINE_INPUT;
    else if (strcmp(name, "echo") == 0)
        bit = ENABLE_ECHO_INPUT;
    if (bit != 0) {
        *mode = negated ? (*mode & ~bit) : (*mode | bit);
        return 0;
    }
    if (strcmp(name, "sane") == 0 || (strcmp(name, "cooked") == 0 && !negated) || (strcmp(name, "raw") == 0 && negated)) {
        *mode |= all;
        return 0;
    }
    if ((strcmp(name, "raw") == 0 && !negated) || (strcmp(name, "cooked") == 0 && negated)) {
        *mode &= ~all;
        return 0;
    }
    if (strcmp(name, "cbreak") == 0) {
        *mode = negated ? (*mode | ENABLE_LINE_INPUT) : (*mode & ~ENABLE_LINE_INPUT);
        return 0;
    }
    fprintf(stderr, PROGRAM ": setting '%s%s' is not supported on a Windows console\n", negated ? "-" : "", name);
    return 1;
}

int main(int argc, char* argv[]) {
    int i, all = 0, save = 0, count = 0, status = 0;
    const char* device = NULL;
    const char** settings = calloc((size_t)argc + 1, sizeof *settings);
    DWORD mode = 0;
    int rows = 0, columns = 0;

    _setmode(_fileno(stdout), _O_BINARY);
    if (settings == NULL) {
        fprintf(stderr, PROGRAM ": memory exhausted\n");
        return 1;
    }
    for (i = 1; i < argc; i++) {
        const char* a = argv[i];

        if (strcmp(a, "--help") == 0) {
            print_usage();
            return 0;
        }
        if (strcmp(a, "--version") == 0) {
            printf(PROGRAM " (Winshell) " VERSION "\n");
            return 0;
        }
        if (strcmp(a, "-a") == 0 || strcmp(a, "--all") == 0) {
            all = 1;
        } else if (strcmp(a, "-g") == 0 || strcmp(a, "--save") == 0) {
            save = 1;
        } else if (strcmp(a, "-F") == 0 && i + 1 < argc) {
            device = argv[++i];
        } else if (strncmp(a, "--file=", 7) == 0) {
            device = a + 7;
        } else if (strcmp(a, "--file") == 0 && i + 1 < argc) {
            device = argv[++i];
        } else {
            settings[count++] = a;
        }
    }
    if (all && save) {
        fprintf(stderr, PROGRAM ": the options for verbose and stty-readable output styles are\nmutually exclusive\n");
        return 1;
    }
    if ((all || save) && count > 0) {
        fprintf(stderr, PROGRAM ": when specifying an output style, modes may not be set\n");
        return 1;
    }

    // the settings are checked before the device is looked at
    for (i = 0; i < count; i++) {
        const char* s = settings[i];
        int negated = s[0] == '-' && s[1] != '\0';
        const char* name = negated ? s + 1 : s;

        if (is_saved_form(s) || is_speed(s))
            continue;
        if (listed(valued, name) && !negated) {
            if (i + 1 >= count) {
                fprintf(stderr, PROGRAM ": missing argument to '%s'\nTry '" PROGRAM " --help' for more information.\n", s);
                return 1;
            }
            i++;
            continue;
        }
        if (listed(flags, name) || (!negated && listed(plain, name)))
            continue;
        invalid_argument(s);
        return 1;
    }

    if (device != NULL) {
        char native[VPATH_MAX];
        DWORD attributes;

        if (vpath_to_native(device, native, sizeof native) != 0 || (attributes = GetFileAttributesA(native)) == INVALID_FILE_ATTRIBUTES) {
            if (_stricmp(device, "/dev/console") != 0 && _stricmp(device, "CON") != 0 && _stricmp(device, "CONIN$") != 0) {
                fprintf(stderr, PROGRAM ": %s: No such file or directory\n", device);
                return 1;
            }
            input = CreateFileA("CONIN$", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        } else {
            (void)attributes;
            input = CreateFileA(native, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        }
        if (input == INVALID_HANDLE_VALUE || !GetConsoleMode(input, &mode)) {
            fprintf(stderr, PROGRAM ": %s: Inappropriate ioctl for device\n", device);
            return 1;
        }
    } else {
        input = GetStdHandle(STD_INPUT_HANDLE);
        if (!GetConsoleMode(input, &mode)) {
            fprintf(stderr, PROGRAM ": 'standard input': Inappropriate ioctl for device\n");
            return 1;
        }
    }

    if (count == 0 || all || save) {
        if (save) {
            printf("winshell:%lx:%lx\n", (unsigned long)mode, (unsigned long)output_mode());
            return 0;
        }
        if (get_size(&rows, &columns))
            printf("speed 38400 baud; rows %d; columns %d; line = 0;\n", rows, columns);
        else
            printf("speed 38400 baud; line = 0;\n");
        print_modes(mode);
        return 0;
    }

    for (i = 0; i < count; i++) {
        const char* s = settings[i];
        int negated = s[0] == '-' && s[1] != '\0';
        const char* name = negated ? s + 1 : s;

        if (is_saved_form(s)) {
            unsigned long in = 0, out = 0;

            if (sscanf(s, "winshell:%lx:%lx", &in, &out) != 2) {
                invalid_argument(s);
                return 1;
            }
            mode = (DWORD)in;
            set_output_mode((DWORD)out);
        } else if (strcmp(s, "size") == 0) {
            if (!get_size(&rows, &columns)) {
                fprintf(stderr, PROGRAM ": 'standard input': Inappropriate ioctl for device\n");
                return 1;
            }
            printf("%d %d\n", rows, columns);
        } else if (strcmp(s, "speed") == 0) {
            printf("38400\n");
        } else if (is_speed(s)) {
            fprintf(stderr, PROGRAM ": setting the speed is not supported on a Windows console\n");
            status = 1;
        } else if (listed(valued, name) && !negated) {
            fprintf(stderr, PROGRAM ": setting '%s' is not supported on a Windows console\n", s);
            status = 1;
            i++;
        } else if (apply(name, negated, &mode) != 0) {
            status = 1;
        }
    }
    if (!SetConsoleMode(input, mode)) {
        fprintf(stderr, PROGRAM ": 'standard input': %s\n", fs_strerror(GetLastError()));
        return 1;
    }
    return status;
}
