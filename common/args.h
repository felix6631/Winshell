#ifndef ARGS_H
#define ARGS_H

/*
 * Command line options of the tools, in the GNU style:
 *
 *   -x -y        short options, which may be grouped: -xy
 *   -oVALUE      a short option with a value; -o VALUE as well
 *   --name       a long option; --name=VALUE or --name VALUE for one with a value
 *   --na         a long option may be shortened as far as it stays unambiguous
 *   -5           a number as an option, when the tool asks for it (head -5)
 *   --           ends the options
 *
 * Options and operands may be mixed (a tool can choose that the first operand
 * ends the options, as env does). --help and --version are always there.
 *
 * The messages are the ones of the GNU tools ("tool: invalid option -- 'x'"),
 * followed by "Try 'tool --help' for more information."
 */

#define ARGS_NONE      0        /* the option has no value */
#define ARGS_REQUIRED  1        /* the option has a value */
#define ARGS_OPTIONAL  2        /* long options only: --name or --name=VALUE */

/** The id with which a number option (-5) is reported; its value is the digits. */
#define ARGS_NUMBER    (-1)

typedef struct {
    int         id;             /* what the handler gets; not 0 (ends the list) and not ARGS_NUMBER */
    char        shortName;      /* 0 when there is none */
    const char* longName;       /* NULL when there is none */
    int         argument;       /* ARGS_NONE, ARGS_REQUIRED or ARGS_OPTIONAL */
} ArgsOption;

typedef struct {
    const char*       program;          /* "head" */
    const char*       version;          /* "0.1" */
    void            (*usage)(void);     /* prints the text of --help */
    const ArgsOption* options;          /* the list ends with an entry whose id is 0 */
    int               usageStatus;      /* the exit status for a bad option: 1 or 2 */
    int               stopAtOperand;    /* the first operand ends the options (env) */
    int               numberOption;     /* -5 is an option (head, tail, uniq) */
} ArgsSpec;

/**
 * Called for each option in the order they were written. value is NULL for an option
 * without one. Returns 0, or -1 after printing its own message (an invalid value).
 */
typedef int (*ArgsHandler)(int id, const char* value, void* context);

#define ARGS_OK     0
#define ARGS_EXIT   1           /* --help or --version was answered: exit with status 0 */
#define ARGS_FAILED (-1)        /* a message was printed: exit with spec->usageStatus */

/** The exit status of a tool whose args_parse did not return ARGS_OK. */
#define ARGS_STATUS(spec, result) ((result) == ARGS_EXIT ? 0 : (spec).usageStatus)

/**
 * Goes through argv[1..argc). The operands are stored in *operands (an array of
 * *operandCount strings, NULL after the last, allocated here; it need not be freed).
 */
int args_parse(const ArgsSpec* spec, int argc, char* argv[], ArgsHandler handler, void* context,
               const char*** operands, int* operandCount);

#endif //ARGS_H
