# Winshell

**English** | [한국어](README.md)

Winshell is a project and toolchain for using a Linux-style shell and coreutils on Windows.
It is written in plain C on top of the Win32 API, and aims to run as far back as Windows XP.

> **Status: early development.** The shell reads a line and runs `bin\<command>.exe`;
> variable expansion and more are not implemented yet.

## Implemented

| Command | Status |
| --- | --- |
| `shell` | Input loop, runs commands with arguments and waits for them, pipes/redirection, `cd`, exits with `quit` |
| `arch` | Done |
| `cat` | Done (`-A -b -e -E -n -s -t -T -u -v`, `--help`, `--version`) |
| `cp` | Done (`-a -f -i -n -p -r -R -t -v`) |
| `echo` | Done (`-n`, `-e`, `-E`, `--help`, `--version`) |
| `head` | Done (`-n -c -q -v -NUM`, `-n -K` leaves out the last K lines) |
| `ls` | Done (`-a -A -C -d -F -h -l -p -r -R -S -t -U -1`, `/` lists the drives) |
| `mkdir` | Done (`-p -v -m`; `-m` is accepted and ignored) |
| `mv` | Done (`-f -i -n -t -v`, to another drive it copies and then removes) |
| `pwd` | Done (`-L`, `-P`, prints the virtual path) |
| `rm` | Done (`-f -i -r -R -d -v`, `/` and the roots of drives and shares are never removed, not even with `--no-preserve-root`) |
| `rmdir` | Done (`-p -v --ignore-fail-on-non-empty`) |
| `tail` | Done (`-n -c -f -s -q -v -NUM`, `-n +K`) |
| `touch` | Done (`-a -c -d -m -r -t`) |
| `wc` | Done (`-l -w -m -c -L`) |

See [Planned](#planned) for what is still to come.

## Using the shell

- Programs are only looked up in the directory of `shell.exe` (`bin\`); the `.exe` extension may be omitted.
- The goal is to do the same job as the Linux commands; only what does not work as is on Windows is adapted to Windows.
  (For example, `cat` does not translate bytes and treats a CR LF pair as one line ending.)
- Exit with `quit`. End of input (Ctrl+Z, Enter) also exits.
- **Quoting**
  - `"a b"`: keeps the spaces. `\"` inside is a literal double quote.
  - `'a b'`: everything inside is taken literally.
  - They can be mixed, as in `a"b c"d`, and `""` gives an empty argument.
  - A backslash is only special in front of a double quote, so paths such as `C:\bin\echo` can be typed as they are.
  - An unclosed quote prints an error and nothing is executed.
- **Wildcards (globbing)**
  - `*` is any string, including none, `?` one character, and `[abc]`, `[a-z]`, `[!abc]` a set of characters.
  - The shell expands them to the matching paths before the program starts, so programs get the expanded names (`ls *.txt`, `cat sub/*.c`).
  - Virtual paths, Windows paths and subdirectories all work (`/c/Users/*`, `*/*.c`, `sub\*.dat`). The matches are written the way the pattern was.
  - A pattern that matches nothing is passed as typed. A wildcard in quotes (`"*.txt"`) is not expanded.
  - Adapted to Windows: names are matched without regard to case, a name starting with a dot is matched only by a pattern that starts with a dot (`.*`), and files with the hidden or system attribute are never matched (so `rm *` does not touch system files).
  - The file names of redirections are not expanded.
- **Pipes and redirection**
  - `a | b`: connects the standard output of a to the standard input of b. All commands run at the same time.
  - `< file`: read standard input from a file.
  - `> file`, `>> file`: write standard output to a file (replacing it), or append to it.
  - `2> file`, `2>> file`: write standard error to a file, or append to it.
  - Spaces around operators are optional (`echo hi>out.txt`). Operators have no meaning inside quotes.
  - A redirection takes precedence over a pipe. The exit code of the last command is the result.
  - `2>&1`, `&`, `&&`, `||` and `;` are not supported yet.
- **Ctrl+C / Ctrl+Break**
  - While a program is running, only that program is interrupted and the shell keeps running.
  - At the prompt the shell does not exit; it discards the line being typed.

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

- The top-level `Makefile` creates `bin\` and then invokes the `Makefile` of each sub-project (`shell`, `arch`, `cat`, `cp`, `echo`, `head`, `ls`, `mkdir`, `mv`, `pwd`, `rm`, `rmdir`, `tail`, `touch`, `wc`).
- Each sub-project's `Makefile` builds the sources in its own directory into `bin\<name>.exe`.
- `shell.exe` only uses the executables located in `bin\` by default.

## Tests

```bat
nmake test     :: build every program, then build and run the tests
```

| Test | What it covers |
| --- | --- |
| `vpath_test` | unit tests of `common/vpath.c`: virtual path conversion, `..`, the root, buffer limits |
| `parser_test` | unit tests of the shell parser: words, quotes, pipes, redirection, syntax errors |
| `commands_test` | runs `echo`, `arch`, `pwd`, `cat`, `ls`, `rm`, `cp`, `mv`, `mkdir`, `rmdir`, `touch`, `head`, `tail` and `wc` as programs and checks output, resulting files and exit codes |
| `shell_test` | feeds scripts to `shell.exe`: running commands, quoting, pipes, redirection, `cd` |

- `nmake test` ends with 0 when everything passes; a failing check is reported with its location and the expected and actual values.
- Test programs and work files are created in `test\out\` (not tracked by git). A single test can be run as `test\out\<name>.exe`.
- Ctrl+C handling is not part of the automatic tests because it needs a real console.
- When adding a command, add its cases to `test\commands_test.c`. Output is compared without telling LF and CRLF apart (`lf_only`).

## Layout

```
Winshell/
├─ Makefile        top-level build rules
├─ shell/          the shell itself (include/, src/)
├─ arch/           arch command
├─ cat/            cat command
├─ cp/             cp command
├─ echo/           echo command
├─ head/           head command
├─ ls/             ls command
├─ mkdir/          mkdir command
├─ mv/             mv command
├─ pwd/            pwd command
├─ rm/             rm command
├─ rmdir/          rmdir command
├─ tail/           tail command
├─ touch/          touch command
├─ wc/             wc command
├─ common/         code shared by several commands (vpath: virtual path conversion, fsutil: copy and remove, textutil: text input)
├─ test/           tests (nmake test)
└─ bin/            build output (not tracked by git)
```

## Adding a new command

1. Create a directory named after the command at the top level and write `main.c` in it.
2. Copy `arch/Makefile` and change `NAME`.
3. Add `build-<name>` and `clean-<name>` to the `all` and `clean` lists in the top-level `Makefile`.

A sub-project's `Makefile` must provide `all` and `clean` rules and accept the output location through the `BINDIR` macro.

## Design goals

- **Virtual root directory:** like a UNIX filesystem there is a logical root directory `/`, and each drive is placed beneath it as a single lower case letter.
  - `C:\Users\me` is `/c/Users/me`. Only the drives are below `/`, and `cd /`, `pwd` and `cat /` all follow that.
  - Windows paths such as `C:\Users\me` keep working everywhere. UNC paths such as `\\server\share` are outside of the virtual tree and are passed to Windows unchanged.
  - `cd` is a builtin of the shell, which tells its children about the current directory through the environment variable `PWD`. While in `/`, the Windows directory is the root of the system drive, and relative paths do not refer to any file.
  - Every command converts the paths it receives itself. When a new command opens a file, use `vpath_to_native` from `common/vpath.h`.
- **Compatibility (goal):** use only basic system calls to stay compatible down to Windows XP. This has not been verified yet.

## Planned

**Shell features**: `2>&1`, variable expansion

**coreutils**

<details>
<summary>Show list</summary>

`[`, b2sum, base32, base64, basename, chgrp, chmod, chown, cksum, comm, csplit, cut, date, dd, df, dir,
dircolors, dirname, du, env, expand, expr, factor, false, fmt, fold, groups, hostid, hostname, id, install,
join, kill, link, ln, logname, md5sum, mkfifo, mknod, mktemp, nice, nl, nohup, nproc, numfmt, od, paste,
pathchk, pinky, pr, printenv, printf, ptx, readlink, realpath, seq, sha1sum, sha224sum, sha256sum,
sha384sum, sha512sum, shred, shuf, sleep, sort, split, stat, stdbuf, sum, sync, tac, tee, test, tr, true,
truncate, tsort, tty, uname, unexpand, uniq, unlink, uptime, users, vdir, who, whoami, yes

</details>

**Out of scope (meaningless or no counterpart on Windows)**: chcon, chroot, runcon, stty, coreutils (multi-call binary)

## License

[Apache License 2.0](LICENSE)
