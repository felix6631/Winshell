# Winshell

**English** | [한국어](README.md)

Winshell is a project and toolchain for using a Linux-style shell and coreutils on Windows.
It is written in plain C on top of the Win32 API, and aims to run as far back as Windows XP.

> **Status: early development.** The shell reads a line and runs `bin\<command>.exe`;
> pipes, redirection and the virtual root directory are not implemented yet.

## Implemented

| Command | Status |
| --- | --- |
| `shell` | Basic input loop, runs commands, exits with `quit` |
| `arch` | Done |
| `echo` | Done (no options) |
| `pwd` | Done (currently prints the plain Windows path) |

See [Planned](#planned) for what is still to come.

## Build

### Requirements

- **The MSVC compiler (`cl.exe`) and `nmake.exe`**
  - Visual Studio or Build Tools for Visual Studio (Desktop development with C++ workload)
- **The path to `cl.exe` must be registered in the `PATH` environment variable.**
  - The easiest way is to build from *x64 Native Tools Command Prompt for VS* or *Developer Command Prompt*,
    which sets up `PATH` as well as `INCLUDE` and `LIB`.
  - From a regular terminal, run `vcvarsall.bat` (or `vcvars64.bat`) first.
  - Check: `where cl` should print a path.

### Build and clean

```bat
nmake          :: build every sub-project
nmake clean    :: remove build output (bin\, obj\)
```

### How the build works

- The top-level `Makefile` creates `bin\` and then invokes the `Makefile` of each sub-project (`shell`, `arch`, `echo`, `pwd`).
- Each sub-project's `Makefile` builds the sources in its own directory into `bin\<name>.exe`.
- `shell.exe` only uses the executables located in `bin\` by default.

## Layout

```
Winshell/
├─ Makefile        top-level build rules
├─ shell/          the shell itself (include/, src/)
├─ arch/           arch command
├─ echo/           echo command
├─ pwd/            pwd command
└─ bin/            build output (not tracked by git)
```

## Adding a new command

1. Create a directory named after the command at the top level and write `main.c` in it.
2. Copy `arch/Makefile` and change `NAME`.
3. Add `build-<name>` and `clean-<name>` to the `all` and `clean` lists in the top-level `Makefile`.

A sub-project's `Makefile` must provide `all` and `clean` rules and accept the output location through the `BINDIR` macro.

## Design goals

- **Virtual root directory (planned):** like a UNIX filesystem, a logical root directory will exist with each drive placed beneath it.
- **Compatibility (goal):** use only basic system calls to stay compatible down to Windows XP. This has not been verified yet.

## Planned

**Shell features**: pipes (`|`), redirection (`<`, `>`), virtual root directory

**coreutils**

<details>
<summary>Show list</summary>

`[`, b2sum, base32, base64, basename, cat, chgrp, chmod, chown, cksum, comm, cp, csplit, cut, date, dd, df, dir,
dircolors, dirname, du, env, expand, expr, factor, false, fmt, fold, groups, head, hostid, hostname, id, install,
join, kill, link, ln, logname, ls, md5sum, mkfifo, mknod, mktemp, mv, nice, nl, nohup, nproc, numfmt, od, paste,
pathchk, pinky, pr, printenv, printf, ptx, readlink, realpath, rm, rmdir, seq, sha1sum, sha224sum, sha256sum,
sha384sum, sha512sum, shred, shuf, sleep, sort, split, stat, stdbuf, sum, sync, tac, tail, tee, test, tr, true,
truncate, tsort, tty, uname, unexpand, uniq, unlink, uptime, users, vdir, wc, who, whoami, yes

</details>

**Out of scope (meaningless or no counterpart on Windows)**: chcon, chroot, runcon, stty, coreutils (multi-call binary)

## License

[Apache License 2.0](LICENSE)
