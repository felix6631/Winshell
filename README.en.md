# Winshell

**English** | [한국어](README.md)

Winshell is a project and toolchain for using a Linux-style shell and coreutils on Windows.
It is written in plain C on top of the Win32 API, and aims to run as far back as Windows XP.

> **Status: early development.** The shell reads a line and runs `bin\<command>.exe`;
> background jobs (`&`), control structures and shell functions are not implemented yet.

## Implemented

| Command | Status |
| --- | --- |
| `shell` | Input loop, runs commands and waits for them, pipes/redirection, `; && \|\|`, variable expansion, wildcards, `cd` `export` `unset` `exit` |
| `arch` | Done |
| `cat` | Done (`-A -b -e -E -n -s -t -T -u -v`, `--help`, `--version`) |
| `cp` | Done (`-a -f -i -n -p -r -R -t -v`) |
| `cut` | Done (`-b -c -f -d -s --complement --output-delimiter`) |
| `echo` | Done (`-n`, `-e`, `-E`, `--help`, `--version`) |
| `head` | Done (`-n -c -q -v -NUM`, `-n -K` leaves out the last K lines) |
| `ls` | Done (`-a -A -C -d -F -h -l -p -r -R -S -t -U -1`, `/` lists the drives) |
| `mkdir` | Done (`-p -v -m`; `-m` is accepted and ignored) |
| `mv` | Done (`-f -i -n -t -v`, to another drive it copies and then removes) |
| `env` | Done (`-i`, `-u`, `-0`, `-C`, `NAME=VALUE`, running a command; found in the directory of `env.exe` or by a virtual path) |
| `printenv` | Done (`-0`, names, status 1 for a missing one) |
| `pwd` | Done (`-L`, `-P`, prints the virtual path) |
| `rm` | Done (`-f -i -r -R -d -v`, `/` and the roots of drives and shares are never removed, not even with `--no-preserve-root`) |
| `rmdir` | Done (`-p -v --ignore-fail-on-non-empty`) |
| `sort` | Done (`-b -d -f -g -i -n -r -s -u -c -C -k -t -o`; byte order as in the C locale; `-h -M -V -z` are not supported) |
| `tac` | Done (`-b -s`, CR LF kept; `-r` is not supported) |
| `tail` | Done (`-n -c -f -s -q -v -NUM`, `-n +K`) |
| `tee` | Done (`-a`; reports a file that fails and goes on; passes data on as it arrives) |
| `nl` | Done (`a t n` for `-b -h -f`, `-n -w -s -v -i -l -p -d`; the `p` regular-expression style is not supported) |
| `paste` | Done (`-d -s -z`; `- -` takes the lines of standard input in turn) |
| `touch` | Done (`-a -c -d -m -r -t`) |
| `tr` | Done (`-c -C -d -s -t`, classes `[:alpha:]` etc.) |
| `uniq` | Done (`-c -d -D -u -i -f -s -w`) |
| `wc` | Done (`-l -w -m -c -L`) |

See [Planned](#planned) for what is still to come.

## Using the shell

- Programs are only looked up in the directory of `shell.exe` (`bin\`); the `.exe` extension may be omitted.
- The goal is to do the same job as the Linux commands; only what does not work as is on Windows is adapted to Windows.
  (For example, `cat` does not translate bytes and treats a CR LF pair as one line ending.)
- Exit with `exit [N]` or `quit`; without N the status of the last command is used. End of input (Ctrl+Z, Enter) also exits.
- **Builtins:** `cd [DIR]`, `export NAME[=VALUE]...` (alone it lists the variables), `unset NAME...`, `exit [N]`, `quit`.
- **Lists of commands**
  - `a ; b`: run b after a, always.
  - `a && b`: run b only when a succeeded (status 0).
  - `a || b`: run b only when a failed.
  - They are evaluated from left to right (`a && b || c` is `(a && b) || c`), and a command that is skipped does not change the status.
  - Each command is parsed when its turn has come, so `cd dir && ls *` sees the directory and the variables the previous command left.
  - The status of a pipeline is that of its last command. A program that is not found gives 127, a syntax error 2, a program stopped with Ctrl+C 130.
- **Assignment**
  - `NAME=value` alone sets a variable of the shell. Only the shell knows it and the programs do not get it; `export NAME` puts it in the environment, and the programs started afterwards have it. Assigning to a variable that is already exported keeps it exported.
  - `NAME=value command` sets the variable in the environment of that command only; the variables of the shell do not change. Only the command position makes an assignment (the `A=1` of `echo A=1` is an argument), and a quote in the name or around the `=` makes it a plain word.
  - The value stays one word and has no wildcards. A `~` at its start is `$HOME`. The blanks of a `$B` in `A=$B` do not split it.
  - Expansion happens before the assignment, so `A=1 echo $A` sees the previous `$A`. `A=1 | cat` in a pipeline does not change the variables of the shell.
- **Variables**
  - `$NAME` and `${NAME}` are replaced by the value of the shell variable or of the environment variable, `$?` by the status of the last command. Names are not case sensitive, like Windows names.
  - In double quotes the value stays one word; outside of quotes it is cut at blanks into words, and wildcards in it act. Inside single quotes nothing is expanded. A variable that is not set makes no word when it stands alone outside of quotes.
  - A `$` that starts no name is just a `$`. Write a literal `$NAME` in single quotes (`\` is the path separator and is no escape).
  - `|`, `>`, `;` and the like in a value are not operators.
  - `PWD` and `OLDPWD` are kept by `cd` as virtual paths. `HOME` and `USER` come from the Windows profile (`USERPROFILE`, `USERNAME`) when they are not defined; `HOME` is a virtual path.
  - A `~` at the start of a word is `$HOME` (`~`, `~/docs`).
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
  - `2>&1`: send standard error where standard output goes. `1>&2` (`>&2`) is the other way round.
  - `&> file`, `&>> file`: write standard output and standard error to the same file.
  - Redirections of standard output and error are applied in the order written: `a > f 2>&1` sends both to f, `a 2>&1 > f` sends the error to the old output and only the output to f.
  - A redirection takes precedence over a pipe. The exit code of the last command is the result.
  - `&` (background jobs) is not supported yet; a lone `&` is a syntax error.
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

- The top-level `Makefile` creates `bin\` and then invokes the `Makefile` of each sub-project (`shell`, `arch`, `cat`, `cp`, `cut`, `echo`, `env`, `head`, `ls`, `mkdir`, `mv`, `nl`, `paste`, `printenv`, `pwd`, `rm`, `rmdir`, `sort`, `tac`, `tail`, `tee`, `touch`, `tr`, `uniq`, `wc`).
- Each sub-project's `Makefile` builds the sources in its own directory into `bin\<name>.exe`.
- `shell.exe` only uses the executables located in `bin\` by default.

## Tests

```bat
nmake test      :: build every program, then build and run the tests
nmake asan      :: the same suite with the programs and the tests built with AddressSanitizer (bin-asan\)
nmake gnu       :: compare the output bytes with the GNU tools (needs bash and GNU coreutils: MSYS2, Git for Windows)
```

The test programs are divided by feature (`test\`).

| Test | What it covers |
| --- | --- |
| `vpath_test` | unit tests of `common/vpath.c`: virtual path conversion, `..`, the root, buffer limits |
| `parser_test` | unit tests of the shell parser: words, quotes, variables, lists of commands, pipes, redirection, syntax errors |
| `args_test` | unit tests of `common/args.c`, the option parser of the tools: grouping, values, abbreviations, number options, errors |
| `basic_test` | `echo`, `arch`, `pwd` |
| `cat_test` | `cat`: bytes kept, options, CR LF |
| `ls_test` | `ls`: sorting, hidden names, the long format, the virtual root |
| `fileops_test` | `rm`, `cp`, `mv`: files and trees, read-only files, the protection of roots |
| `dirs_test` | `mkdir`, `rmdir`, `touch` |
| `text_test` | `head`, `tail`, `wc` |
| `filter_test` | `sort`, `uniq`, `cut`, `tr` |
| `lines_test` | `tee`, `tac`, `nl`, `paste`: line endings, separators, sections, standard input taken in turn |
| `env_test` | `env`, `printenv`: changing the environment, `-i`/`-u`, running a command and its status, `-C` |
| `shell_run_test` | shell: running commands, quoting, pipes, redirection |
| `shell_cd_test` | shell: `cd`, the virtual file system, wildcards |
| `shell_lists_test` | shell: `; && \|\|`, exit status, variables, `exit` |
| `shell_pipeline_test` | shell: the commands together with pipes, redirection and wildcards |
| `equiv_test` | **property test**: random inputs give the same results with LF, CR LF and mixed line endings (`WS_ITER`, `WS_SEED`) |
| `limits_test` | **limits**: sorting 500,000 lines, one line of 20 MB, sizes around the buffers (65536 +- 1), 2,000,000 empty lines, deep paths, many files (`WS_STRESS=1`: ten times larger) |
| `sharing_test` | **sharing violations**: `rm`/`mv`/`cp` of files and directories another process holds open, moves between volumes |
| `async_test` | **asynchronous**: input that comes in pieces, programs that wait for the end of their input, `tail -f` |

- `nmake test` ends with 0 when everything passes; a failure names the program, the location of the check and the expected and actual values.
- Test programs and work files are created in `test\out\` (not tracked by git). A single test can be run as `test\out\<name>.exe`.
- **AddressSanitizer:** `nmake asan` rebuilds all tools and tests with `/fsanitize=address` (`bin-asan\`, `test\out-asan\`) and runs them. Memory errors are reported with their location and fail the test. The environment variable `WINSHELL_BIN` selects the directory of tools the tests use.
- **Comparison with GNU:** `test\gnu_compare.sh` compares `head`, `tail`, `wc`, `sort`, `uniq`, `cut`, `tr` and `cat` with the GNU tools in about a thousand combinations of options and inputs (`LC_ALL=C`). What is meant to differ (a CR LF ending is not part of the line, `sort -z/-h/-M/-V` are refused, characters are UTF-8) is not compared.
- **`mv` to another drive:** with the environment variable `WINSHELL_TEST_CROSS_DEVICE` set, `mv` does not try the rename and takes the copy-and-remove path. The tests run that code on one volume with it, and for real when a second fixed drive that can be written to exists.
- When adding a command, add its cases to the `*_test.c` of its feature. The shared parts are `testlib.h` (checks, running programs) and `testtools.h` (work directory, running tools, fixtures). Output is compared without telling LF and CRLF apart (`lf_only`).
- Ctrl+C handling is not part of the automatic tests because it needs a real console.

## Layout

```
Winshell/
├─ Makefile        top-level build rules
├─ shell/          the shell itself (include/, src/)
├─ arch/           arch command
├─ cat/            cat command
├─ cp/             cp command
├─ cut/            cut command
├─ echo/           echo command
├─ env/            env command
├─ head/           head command
├─ ls/             ls command
├─ mkdir/          mkdir command
├─ mv/             mv command
├─ printenv/       printenv command
├─ nl/             nl command
├─ paste/          paste command
├─ pwd/            pwd command
├─ rm/             rm command
├─ rmdir/          rmdir command
├─ sort/           sort command
├─ tac/            tac command
├─ tail/           tail command
├─ tee/            tee command
├─ touch/          touch command
├─ tr/             tr command
├─ uniq/           uniq command
├─ wc/             wc command
├─ common/         code shared by several commands (vpath: virtual path conversion, fsutil: copy and remove, textutil: text input and lines)
├─ test/           tests (nmake test)
└─ bin/            build output (not tracked by git)
```

## Adding a new command

1. Create a directory named after the command at the top level and write `main.c` in it. Handle the options with
   `args_parse` of `common/args.h` (`tee/main.c` is the smallest example: a table of options, a handler, one call).
2. Copy `tee/Makefile` and change `NAME`, `SRCS` and `HDRS`.
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

**Shell features**: background jobs (`&`), brace expansion, command substitution (`$(...)`), `if`/`for`/`while`

**coreutils**

<details>
<summary>Show list</summary>

`[`, b2sum, base32, base64, basename, chgrp, chmod, chown, cksum, comm, csplit, date, dd, df, dir,
dircolors, dirname, du, expand, expr, factor, false, fmt, fold, groups, hostid, hostname, id, install,
join, kill, link, ln, logname, md5sum, mkfifo, mknod, mktemp, nice, nohup, nproc, numfmt, od,
pathchk, pinky, pr, printf, ptx, readlink, realpath, seq, sha1sum, sha224sum, sha256sum,
sha384sum, sha512sum, shred, shuf, sleep, split, stat, stdbuf, sum, sync, test, true,
truncate, tsort, tty, uname, unexpand, unlink, uptime, users, vdir, who, whoami, yes

</details>

**Out of scope (meaningless or no counterpart on Windows)**: chcon, chroot, runcon, stty, coreutils (multi-call binary)

## License

[Apache License 2.0](LICENSE)
