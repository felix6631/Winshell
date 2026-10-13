#ifndef FIND_H
#define FIND_H

#include <stddef.h>
#include <windows.h>
#include "regex.h"

/*
 * find: the expression (parse.c) and the walk that evaluates it (eval.c) meet in these types.
 */

typedef enum {
    P_TRUE, P_FALSE,
    P_NAME, P_INAME, P_PATH, P_IPATH, P_REGEX,
    P_TYPE, P_XTYPE, P_SIZE, P_EMPTY, P_NEWER, P_TIME, P_MIN,
    P_READABLE, P_WRITABLE, P_EXECUTABLE,
    P_PRUNE, P_QUIT, P_OPTION,                  // P_OPTION: -maxdepth and the like, always true
    P_PRINT, P_PRINT0, P_PRINTF, P_EXEC, P_DELETE
} PredKind;

typedef enum { N_TEST, N_NOT, N_AND, N_OR, N_COMMA } NodeKind;

typedef struct Node {
    NodeKind     kind;
    struct Node* left;
    struct Node* right;

    PredKind     pred;
    const char*  text;          // the pattern, or the format of -printf
    long long    number;        // -size: in units; -mtime and the like: the number
    int          compare;       // -1 less than (-N), 0 exactly (N), 1 more than (+N)
    char         unit;          // -size: c w b k M G; -mtime and the like: a c m
    char         type;          // -type
    Regex*       regex;
    FILETIME     reference;     // -newer

    char**       argv;          // -exec: the command, with {} where the path goes
    int          argc;
    int          batch;         // ends with {} + : run once for many paths
    int          inDirectory;   // -execdir and -okdir
    int          confirm;       // -ok and -okdir
    char**       pending;       // the paths of a batch not run yet
    size_t       pendingCount, pendingCapacity;
    char*        pendingDirectory;
} Node;

typedef struct {
    int maxDepth;               // -1: no limit
    int minDepth;
    int depthFirst;             // -depth
    int follow;                 // -L
    int hasAction;              // the expression prints or does something: no implicit -print
} FindOptions;

/**
 * Parses the arguments after the paths. Returns the expression (NULL when there is none), or sets *error
 * and returns NULL with a message printed. On success *ok is 1.
 */
Node* find_parse(int argc, char* argv[], FindOptions* options, int* ok);

/** Walks each start point and evaluates the expression. Returns the exit status (0, or 1 on errors). */
int find_walk(Node* expression, const FindOptions* options, const char* const* starts, int startCount);

#endif //FIND_H
