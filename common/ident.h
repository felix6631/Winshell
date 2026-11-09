#ifndef IDENT_H
#define IDENT_H

/*
 * Who a user is, as id and groups show it. Windows has no numeric user and group IDs: the ID is the last part of the
 * security identifier (the relative ID, the 197609 of S-1-5-21-...-197609), the name is the name of the account.
 * The groups are the ones the user belongs to; the primary group comes first.
 */

#define IDENT_MAX_GROUPS 512

typedef struct {
    unsigned id;
    char     name[256];
} IdentEntry;

typedef struct {
    IdentEntry user;
    IdentEntry primary;
    int        groupCount;                     /* the groups, the primary group first */
    IdentEntry groups[IDENT_MAX_GROUPS];
} Identity;

/** The user the process runs as. Returns 0, or -1 (nothing found). */
int ident_current(Identity* out);

/** The user with this name (or this ID, for the current user). Returns 0, or -1 when there is no such user. */
int ident_user(const char* name, Identity* out);

#endif //IDENT_H
