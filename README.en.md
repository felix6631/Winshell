<div align="center">

# Winshell

**A bash-style shell and GNU-style coreutils that run natively on Windows.**<br>
Written from scratch in plain C on top of the Win32 API.

**English** | [한국어](README.md)

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![Platform: Windows](https://img.shields.io/badge/platform-Windows-0078D6.svg)
![Language: C](https://img.shields.io/badge/language-C-555555.svg)
![Runtime deps: none](https://img.shields.io/badge/runtime%20deps-none-brightgreen.svg)

</div>

<!-- TODO: demo GIF (e.g. docs/demo.gif) — a 20-second recording of shell.exe with grep, find, pipes and non-ASCII file names -->

```sh
cd ~/projects                          # C:\Users\me\projects is /c/Users/me/projects
grep -rn TODO --include='*.c' . | head -3
find . -name '*.log' -mtime +7 -delete
ls *.md | xargs wc -l
for f in report*.txt; do n=$(wc -l < "$f"); echo "$f: $n lines"; done
```

## Why Winshell

- **No emulation layer.** Unlike MSYS2 or Cygwin, there is no POSIX emulation DLL underneath: every command is a standalone
  executable that calls the Win32 API directly. They only use DLLs that ship with Windows (`KERNEL32` and friends) and link the
  C runtime statically, so no Visual C++ redistributable is needed either. Each executable is 110–260 KB.
- **Same output as GNU.** 29 tools are checked **byte for byte against the real GNU tools** over 3,000+ combinations of options
  and inputs (`nmake gnu`). `diff` uses the same Myers algorithm as GNU diff, so it picks the same answer when several are equally short.
- **A shell that understands Windows.** Both `/c/Users/me` and `C:\Users\me` work, CRLF files keep their line endings,
  and non-ASCII file names and globs like `[가-힣]` match characters, not bytes.
- **A real shell language.** Pipes and redirection, plus `if`/`while`/`for`/`case`, functions with `local`, `$(...)`, `$((...))`,
  `${NAME:-value}`, `trap`, `read` and scripts.
- **Readable code.** About 39k lines of C (tests included), with its own regex engine, date parser and option parser.
  A good place to learn how a shell and coreutils actually work.

### How it compares

|  | **Winshell** | Git Bash (MSYS2) | Cygwin | WSL |
| --- | --- | --- | --- | --- |
| How it runs | Native Win32 executables | GNU tools on `msys-2.0.dll` | GNU tools on `cygwin1.dll` | A Linux distribution on a Linux kernel |
| What you need | One `bin\` folder | Git for Windows | A Cygwin install | The WSL feature + a distribution |
| Default path style | `/c/...` (`C:\...` works as is) | `/c/...` | `/cygdrive/c/...` | `/mnt/c/...` |
| GNU compatibility | Main options, verified against GNU output | GNU itself | GNU itself | GNU itself |

If you need every last feature of the GNU tools, Git Bash or WSL is the better fit. Winshell aims to be **light, install-free
and at home on Windows**.

## Quick start

There are no prebuilt binaries yet, so build it yourself. You need **Visual Studio** or **Build Tools for Visual Studio**
(the "Desktop development with C++" workload).

From a *Developer Command Prompt for VS*:

```bat
git clone https://github.com/felix6631/Winshell.git
cd Winshell
nmake
bin\shell.exe
```

The shell only looks for commands in the `bin\` next to it, so there is no `PATH` to change and no clash with Windows' own `find` and `sort`.

```sh
fact() {
  if [ $1 -le 1 ]; then echo 1; return; fi
  p=$(fact $(($1 - 1)))
  echo $(($1 * p))
}
fact 10        # 3628800
```

## Commands

The shell (`shell`) and 43 commands. Every supported option is listed in the [reference](docs/REFERENCE.en.md#implemented).

| Group | Commands |
| --- | --- |
| Files and directories | `ls` `cp` `mv` `rm` `mkdir` `rmdir` `touch` `find` `realpath` `basename` `dirname` `pwd` |
| Text processing | `cat` `head` `tail` `grep` `sed` `sort` `uniq` `cut` `tr` `wc` `nl` `paste` `tac` `tee` `comm` `diff` |
| Output and arithmetic | `echo` `printf` `expr` `date` |
| Scripting helpers | `test` `[` `true` `false` `xargs` `env` `printenv` `which` `sleep` |
| Processes and system | `ps` `kill` `arch` |

<details>
<summary><b>Implementation highlights</b></summary>

- **`diff`**: the algorithm of GNU diff (Myers middle snake, common ends, shifting of runs, heuristics for big inputs). Normal, `-u` and `-c` formats, and directories.
- **Regex engine** (`common/regex.c`): basic and extended syntax, back-references, leftmost-longest matching, UTF-8 characters. Shared by `grep`, `sed`, `find` and `expr`.
- **`sed`**: nearly every command (`s y p P d D n N g G h H x l = a i c r R w W q Q b t T z F` …), `-i[SUFFIX]`, `first~step` and `0,/re/` addresses.
- **`date -d`**: `3 weeks ago`, `next month`, `yesterday`, ISO dates, `@seconds`; years before 1970 and after 3000.
- **`expr`**: integers of any size.
- **`find`**: `-exec ... {} +`, `-execdir`, `-printf`, `-delete`, `-prune`. **`xargs -P`** runs in parallel.
- **`kill`**: Windows has no signals, so `STOP`/`CONT` suspend and resume, `KILL` terminates at once, and the rest send `WM_CLOSE` to the window.
- **Safety**: globs do not match hidden or system files (`rm *` leaves them alone), and `rm` refuses to remove the root of a drive or a share.

</details>

## Tests

```bat
nmake test      :: run the 38 test programs in parallel
nmake asan      :: rebuild every tool and test with AddressSanitizer and run them
nmake gnu       :: compare the output with GNU (needs bash from MSYS2 or Git for Windows; about 30 seconds)
```

Besides the unit tests there are **property tests** (LF, CRLF and mixed line endings give the same result), **limit tests**
(sorting 500,000 lines, a 20 MB line), **sharing-violation tests** (files held open by other processes) and **asynchronous tests**
(input that arrives in pieces). See the [reference](docs/REFERENCE.en.md#tests) for the details.

## Documentation

- [Reference](docs/REFERENCE.en.md): the options of every command, the shell language, the build, the tests, the layout and the design
- [Adding a new command](docs/REFERENCE.en.md#adding-a-new-command): `nmake new NAME=<name>` creates the skeleton, the test and the GNU comparison in one go

## Roadmap

Winshell is under active development.

- **Shell**: background jobs (`&`), brace expansion, `${NAME/pattern/value}`, `$$`, arrays, functions and control structures inside pipelines, `select`, more signals for `trap`
- **Compatibility**: the goal is to run as far back as Windows XP (not verified yet). UTF-8 behaviour needs Windows 10 version 1903 or later.
- **coreutils**

<details>
<summary>Show the planned commands</summary>

b2sum, base32, base64, chgrp, chmod, chown, cksum, csplit, dd, df, dir, dircolors, du, expand, factor, fmt, fold,
groups, hostid, hostname, id, install, join, link, ln, logname, md5sum, mkfifo, mknod, mktemp, nice, nohup, nproc,
numfmt, od, pathchk, pinky, pr, ptx, readlink, seq, sha1sum, sha224sum, sha256sum, sha384sum, sha512sum, shred,
shuf, split, stat, stdbuf, sum, sync, truncate, tsort, tty, uname, unexpand, unlink, uptime, users, vdir, who,
whoami, yes

Out of scope (meaningless or no counterpart on Windows): chcon, chroot, runcon, stty, coreutils (the multi-call binary)

</details>

## Contributing

Issues and pull requests are welcome. Small commands such as `yes`, `seq`, `whoami`, `base64` or `fold` are a good first step.

```bat
nmake new NAME=yes      :: creates yes\main.c, test\yes_test.c, test\gnu\yes.sh and the reference entries
nmake test              :: run the tests
nmake check-registry    :: check that nothing is missing from the references and the tests
```

## License

[MIT](LICENSE)
