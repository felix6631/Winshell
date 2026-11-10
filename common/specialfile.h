#ifndef SPECIALFILE_H
#define SPECIALFILE_H

/*
 * mkfifo and mknod: Windows has no FIFOs and no device files in its file systems, so nothing of the kind can be made. What
 * the tools can still do as UNIX does is to check the name: a name that exists is "File exists", one in a directory that
 * does not exist is "No such file or directory" (or "Not a directory"); in every other case the file system would be asked
 * to make the file, and here it answers "Operation not supported".
 */

/** The reason the file NAME cannot be made, as strerror words ("File exists", ...). Never NULL. */
const char* special_file_reason(const char* name);

#endif //SPECIALFILE_H
