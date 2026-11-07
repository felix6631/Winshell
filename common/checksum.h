#ifndef CHECKSUM_H
#define CHECKSUM_H

#include "hash.h"

/**
 * The whole of md5sum, sha1sum, sha224sum, sha256sum, sha384sum and sha512sum: the command line (-b -c -t -w -z
 * --tag --quiet --status --strict --ignore-missing), writing "HASH *NAME" lines (or the BSD form with --tag) and
 * checking them with -c. `tag` is the name of the algorithm in the BSD form ("MD5", "SHA256"). Returns the exit status.
 */
int checksum_main(const char* program, const char* version, const char* tag, HashAlgorithm algorithm, int argc, char* argv[]);

#endif //CHECKSUM_H
