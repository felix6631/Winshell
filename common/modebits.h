#ifndef MODEBITS_H
#define MODEBITS_H

/*
 * File modes as chmod and install write them: octal ("644") or symbolic ("u+x,go-w", "a=rX", "g=u"). The adjustment
 * follows gnulib's mode_adjust, so a mode without who is limited by the umask and X, s and t mean what they mean there.
 * Windows keeps only one of the bits (the write permission of the owner is the read-only attribute): see mode_of_file.
 */

#define MODE_SETUID  04000
#define MODE_SETGID  02000
#define MODE_STICKY  01000

typedef struct ModeChange ModeChange;

/** Compiles a mode string. Returns NULL when it is not a mode. */
ModeChange* mode_compile(const char* text);

void mode_free(ModeChange* changes);

/** The mode that results from oldMode (permission bits only); directory tells X and the special bits. */
unsigned mode_adjust(unsigned oldMode, int directory, unsigned umaskValue, const ModeChange* changes);

/** "rw-r--r--": nine characters and the end, with s, S, t and T for the special bits. */
void mode_text9(unsigned mode, char* out);

/**
 * The mode Windows gives a file: a directory 0755, a file 0644 (0444 when it is read-only) and +111 for a program
 * (.exe .bat .cmd .com .ps1). `name` is the name the extension is taken from.
 */
unsigned mode_of_attributes(unsigned long attributes, const char* name);

#endif //MODEBITS_H
