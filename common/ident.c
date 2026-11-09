/*
 * ident: the user and the groups of an account, from the access token (the current user) or the account database.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <lm.h>
#include "ident.h"

static unsigned sid_id(PSID sid) {
    UCHAR count = *GetSidSubAuthorityCount(sid);

    return count == 0 ? 0 : (unsigned)*GetSidSubAuthority(sid, (DWORD)(count - 1));
}

static int sid_name(PSID sid, char* name, size_t size, SID_NAME_USE* use) {
    char domain[256];
    DWORD nameSize = (DWORD)size, domainSize = sizeof domain;
    SID_NAME_USE kind;

    if (!LookupAccountSidA(NULL, sid, name, &nameSize, domain, &domainSize, &kind))
        return -1;
    if (use != NULL)
        *use = kind;
    return 0;
}

static void set_entry(IdentEntry* e, PSID sid) {
    e->id = sid_id(sid);
    if (sid_name(sid, e->name, sizeof e->name, NULL) != 0)
        e->name[0] = '\0';
}

/** Adds a group unless the primary group (or an earlier one) is the same; sids keeps the ones seen. */
static void add_group(Identity* out, PSID* seen, PSID sid, int any) {
    int i;
    SID_NAME_USE use;
    char name[256];

    if (out->groupCount >= IDENT_MAX_GROUPS)
        return;
    for (i = 0; i < out->groupCount; i++) {
        if (EqualSid(seen[i], sid))
            return;
    }
    if (sid_name(sid, name, sizeof name, &use) != 0)
        return;
    if (!any && use != SidTypeGroup && use != SidTypeAlias && use != SidTypeWellKnownGroup)
        return;
    seen[out->groupCount] = sid;
    set_entry(&out->groups[out->groupCount], sid);
    out->groupCount++;
}

int ident_current(Identity* out) {
    HANDLE token;
    DWORD size = 0;
    TOKEN_USER* user = NULL;
    TOKEN_PRIMARY_GROUP* primary = NULL;
    TOKEN_GROUPS* groups = NULL;
    static PSID seen[IDENT_MAX_GROUPS];
    int result = -1;
    DWORD i;

    memset(out, 0, sizeof *out);
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return -1;
    GetTokenInformation(token, TokenUser, NULL, 0, &size);
    user = malloc(size == 0 ? 1 : size);
    if (user == NULL || !GetTokenInformation(token, TokenUser, user, size, &size))
        goto done;
    set_entry(&out->user, user->User.Sid);
    size = 0;
    GetTokenInformation(token, TokenPrimaryGroup, NULL, 0, &size);
    primary = malloc(size == 0 ? 1 : size);
    if (primary == NULL || !GetTokenInformation(token, TokenPrimaryGroup, primary, size, &size))
        goto done;
    set_entry(&out->primary, primary->PrimaryGroup);
    add_group(out, seen, primary->PrimaryGroup, 1);
    size = 0;
    GetTokenInformation(token, TokenGroups, NULL, 0, &size);
    groups = malloc(size == 0 ? 1 : size);
    if (groups == NULL || !GetTokenInformation(token, TokenGroups, groups, size, &size))
        goto done;
    for (i = 0; i < groups->GroupCount; i++) {
        DWORD attributes = groups->Groups[i].Attributes;

        if (!(attributes & SE_GROUP_ENABLED) || (attributes & (SE_GROUP_LOGON_ID | SE_GROUP_USE_FOR_DENY_ONLY)))
            continue;
        add_group(out, seen, groups->Groups[i].Sid, 0);
    }
    result = 0;
done:
    free(user);
    free(primary);
    free(groups);
    CloseHandle(token);
    return result;
}

static int all_digits(const char* s) {
    if (*s == '\0')
        return 0;
    for (; *s != '\0'; s++) {
        if (!isdigit((unsigned char)*s))
            return 0;
    }
    return 1;
}

/** The ID of the account with this name, or -1. */
static int account_entry(const char* name, IdentEntry* e) {
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof sidBuffer, domainSize = 0;
    char domain[256];
    SID_NAME_USE use;

    domainSize = sizeof domain;
    if (!LookupAccountNameA(NULL, name, (PSID)sidBuffer, &sidSize, domain, &domainSize, &use))
        return -1;
    set_entry(e, (PSID)sidBuffer);
    return 0;
}

int ident_user(const char* name, Identity* out) {
    BYTE sidBuffer[SECURITY_MAX_SID_SIZE];
    DWORD sidSize = sizeof sidBuffer, domainSize;
    char domain[256];
    SID_NAME_USE use;
    Identity current;
    wchar_t wide[256];
    USER_INFO_4* info = NULL;
    LOCALGROUP_USERS_INFO_0* local = NULL;
    DWORD read = 0, total = 0, i;
    int haveCurrent = ident_current(&current) == 0;

    memset(out, 0, sizeof *out);
    if (haveCurrent && all_digits(name) && (unsigned)strtoul(name, NULL, 10) == current.user.id) {
        *out = current;
        return 0;
    }
    domainSize = sizeof domain;
    if (!LookupAccountNameA(NULL, name, (PSID)sidBuffer, &sidSize, domain, &domainSize, &use) || use != SidTypeUser)
        return -1;
    if (haveCurrent && _stricmp(name, current.user.name) == 0) {
        *out = current;
        return 0;
    }
    set_entry(&out->user, (PSID)sidBuffer);
    out->primary.id = 0;
    // the local account database: the primary group and the groups of the user
    if (MultiByteToWideChar(CP_ACP, 0, out->user.name, -1, wide, 256) == 0)
        return 0;
    if (NetUserGetInfo(NULL, wide, 4, (LPBYTE*)&info) == NERR_Success && info != NULL) {
        unsigned rid = info->usri4_primary_group_id;
        PSID user = (PSID)sidBuffer;
        UCHAR count = *GetSidSubAuthorityCount(user);
        BYTE copy[SECURITY_MAX_SID_SIZE];

        CopySid(sizeof copy, (PSID)copy, user);
        *GetSidSubAuthority((PSID)copy, (DWORD)(count - 1)) = rid;
        out->primary.id = rid;
        if (sid_name((PSID)copy, out->primary.name, sizeof out->primary.name, NULL) != 0)
            out->primary.name[0] = '\0';
        out->groups[0] = out->primary;
        out->groupCount = 1;
        NetApiBufferFree(info);
    }
    if (NetUserGetLocalGroups(NULL, wide, 0, LG_INCLUDE_INDIRECT, (LPBYTE*)&local, MAX_PREFERRED_LENGTH, &read, &total)
            == NERR_Success && local != NULL) {
        for (i = 0; i < read && out->groupCount < IDENT_MAX_GROUPS; i++) {
            char group[256];
            IdentEntry e;
            int j, dup = 0;

            if (WideCharToMultiByte(CP_ACP, 0, local[i].lgrui0_name, -1, group, sizeof group, NULL, NULL) == 0)
                continue;
            if (account_entry(group, &e) != 0)
                continue;
            for (j = 0; j < out->groupCount; j++) {
                if (strcmp(out->groups[j].name, e.name) == 0)
                    dup = 1;
            }
            if (!dup)
                out->groups[out->groupCount++] = e;
        }
        NetApiBufferFree(local);
    }
    return 0;
}
