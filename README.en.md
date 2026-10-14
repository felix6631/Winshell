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
| `basename` | Done (`-a -s -z`; both `/` and `\` separate; the suffix is removed unless it is the whole name) |
| `date` | Done (`-d -u -r -I -R --rfc-3339`, the format letters `%a %A %b %B %c %C %d %D %e %F %g %G %H %I %j %k %l %m %M %N %p %P %q %r %R %s %S %T %u %U %V %w %W %x %X %y %Y %z %:z %Z` with the flags `- _ 0 ^ #` and a width; `-d` reads `@SECONDS`, ISO dates and times, month names, `now yesterday tomorrow`, `+2 days`, `3 weeks ago`, `next month`, weekdays; years before 1970 and after 3000 work; `-s` (setting the clock) is not supported) |
| `diff` | Done (normal / `-u -U N` / `-c -C N` / `-NUM`, `-q -s -i -w -b -B -a -r -N -x --label --strip-trailing-cr`; the algorithm of GNU diff (Myers middle snake, the common ends, the shifting of runs, the heuristics for big inputs) so that the same one of several equal results is picked; directories; exit status 0/1/2; not there: `-y -e -p --color` and more) |
| `dirname` | Done (`-z`; several names) |
| `cat` | Done (`-A -b -e -E -n -s -t -T -u -v`, `--help`, `--version`) |
| `cp` | Done (`-a -f -i -n -p -r -R -t -v`) |
| `cut` | Done (`-b -c -f -d -s --complement --output-delimiter`) |
| `echo` | Done (`-n`, `-e`, `-E`, `--help`, `--version`) |
| `expr` | Done (`| & < <= = == != >= > + - * / % :`, `match substr index length +`, `( )`; integers of any size, numeric comparison when both are integers, UTF-8 characters, exit status 0/1/2/3) |
| `realpath` | Done (`-e -m -s -q -z --relative-to --relative-base`; by default only the last component may be missing, junctions and symbolic links are followed, results are virtual paths `/c/...`) |
| `sed` | Done (`-n -e -f -E -r -i[SUFFIX] -s -z -l -u --sandbox`; commands `s y p P d D n N g G h H x l = a i c r R w W q Q b t T z F : { }`, addresses `N $ /re/ first~step addr,+N addr,~N 0,/re/ !`, flags of `s` `g p N I w`, `\1`..`\9 & \U \L \u \l \E`; CR LF endings and a last line without an ending are kept, UTF-8 characters; the `e` command and the `M` flag are not supported) |
| `find` | Done (`-H -L -P`, operators `( ) ! -a -o ,`, options `-maxdepth -mindepth -depth -follow`, tests `-name -iname -path -ipath -regex -iregex -regextype -type -xtype -size -empty -newer -mtime -atime -ctime -mmin -amin -cmin -readable -writable -executable`, actions `-print -print0 -printf -delete -prune -quit -exec -execdir -ok` (with `;` and `+`); `-perm -user -group -links -inum -ls -fprint` are not supported) |
| `which` | Done (`-a -s`; the directory of the programs of the project first, then the directories of `PATH` with the extensions of `PATHEXT`; a name with a path is checked as it is; virtual paths as output; exit status 0/1/2, 255 with no name) |
| `xargs` | Done (`-0 -a -d -E -e -I -i -L -l -n -P -p -r -s -t -x --show-limits`; quotes and backslashes, lines continued by a trailing blank, a command line limit of 30000 characters, parallel runs with `-P`; exit status 0/123/124/126/127/1) |
| `grep` | Done (`-E -F -G -e -f -i -v -w -x -c -l -L -m -n -H -h -o -q -s -b -r -A -B -C -NUM -a -I -z -Z --include --exclude --exclude-dir`; basic and extended expressions (with back references), leftmost-longest matches, UTF-8 characters; `-P` and colours are not supported) |
| `head` | Done (`-n -c -q -v -NUM`, `-n -K` leaves out the last K lines) |
| `ls` | Done (`-a -A -C -d -F -h -l -p -r -R -S -t -U -1`, `/` lists the drives) |
| `mkdir` | Done (`-p -v -m`; `-m` is accepted and ignored) |
| `mv` | Done (`-f -i -n -t -v`, to another drive it copies and then removes) |
| `env` | Done (`-i`, `-u`, `-0`, `-C`, `NAME=VALUE`, running a command; found in the directory of `env.exe` or by a virtual path) |
| `printenv` | Done (`-0`, names, status 1 for a missing one) |
| `kill` | Done (`-s -n -SIGNAL -l -L`; Windows has no signals: 0 only checks, STOP/TSTP/CONT suspend and resume, KILL ends at once (status 137), CHLD/URG/WINCH do nothing, any other closes the windows with WM_CLOSE or ends a process without windows (status 128+number); no process groups) |
| `ps` | Done (selection `-e -A -a -x -p --ppid -u -C -N`, BSD style `aux`; formats `-f u -o NAME=HEADING`, `--sort`, `--no-headers`; by default the processes of this console; columns pid ppid user comm args time etime start tty nlwp rss vsz pcpu pmem c ni; unknown values show `-`/`?`; status 1 when nothing was selected) |
| `pwd` | Done (`-L`, `-P`, prints the virtual path) |
| `rm` | Done (`-f -i -r -R -d -v`, `/` and the roots of drives and shares are never removed, not even with `--no-preserve-root`) |
| `rmdir` | Done (`-p -v --ignore-fail-on-non-empty`) |
| `sleep` | Done (`NUMBER[s|m|h|d]...` added up, fractions allowed) |
| `sort` | Done (`-b -d -f -g -i -n -r -s -u -c -C -k -t -o`; byte order as in the C locale; `-h -M -V -z` are not supported) |
| `tac` | Done (`-b -s`, CR LF kept; `-r` is not supported) |
| `tail` | Done (`-n -c -f -s -q -v -NUM`, `-n +K`) |
| `tee` | Done (`-a`; reports a file that fails and goes on; passes data on as it arrives) |
| `nl` | Done (`a t n` for `-b -h -f`, `-n -w -s -v -i -l -p -d`; the `p` regular-expression style is not supported) |
| `paste` | Done (`-d -s -z`; `- -` takes the lines of standard input in turn) |
| `touch` | Done (`-a -c -d -m -r -t`; `-d` uses the same reader of dates as `date -d` (`common/datetime.c`): `@SECONDS`, ISO dates and times, month names, `yesterday`, `3 days ago`, zones) |
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
  - `PWD` and `OLDPWD` are kept as virtual paths by `shell/src/cwd.c` (only `cd` changes them); they are exported, so the children get them. `HOME` and `USER` come from the Windows profile (`USERPROFILE`, `USERNAME`) when they are not defined; `HOME` is a virtual path.
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

- The top-level `Makefile` creates `bin\` and then invokes the `Makefile` of each sub-project (`shell`, `arch`, `basename`, `cat`, `cp`, `cut`, `date`, `diff`, `dirname`, `echo`, `env`, `expr`, `find`, `grep`, `head`, `kill`, `ls`, `mkdir`, `mv`, `nl`, `paste`, `printenv`, `ps`, `pwd`, `realpath`, `rm`, `rmdir`, `sed`, `sleep`, `sort`, `tac`, `tail`, `tee`, `touch`, `tr`, `uniq`, `wc`, `which`, `xargs`).
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
| `regex_test` | unit tests of `common/regex.c`: basic and extended syntax, longest match, sets and classes, word boundaries, back references, UTF-8, error messages |
| `clock_test` | `sleep`, `date`: waiting, formats, how `-d` reads, ISO weeks, exit status |
| `which_test` | `which`: the `bin` of the project, `PATH`, `PATHEXT`, directories, names with a path, `-a`, exit status |
| `proc_test` | `kill`, `ps`: selection, formats, sorting, signals (exit status 128+number, suspend/resume) and errors against real processes (sleep) |
| `diff_test` | `diff`: the three formats, ignoring options, line endings, a last line without line feed, standard input, directories, large inputs, exit status |
| `realpath_test` | `realpath`: virtual paths, what has to exist, relative paths, junctions |
| `names_test` | `basename`, `dirname`: separators (also `\`), suffixes, NUL endings, the root |
| `xargs_test` | `xargs`: batches, how items are read, files, exit status, parallel runs |
| `find_test` | `find`: order of the walk, depth, `-prune`, `-exec`/`-execdir`/`-ok`, `-delete`, virtual paths, Korean names, exit status |
| `expr_test` | `expr`: arithmetic, comparison, logic, string functions, big integers, exit status |
| `sed_test` | `sed`: substitution, addresses, commands, `-i` and backups, `w`/`r` files, CR LF and the end of the last line, exit status, UTF-8 |
| `grep_test` | `grep`: CR LF, UTF-8, binary files, `-r`, context, exit status, input that comes as it is written |
| `shell_utf8_test` | shell: Korean names and directories, wildcards that match a character, variables, redirection (Windows 10 1903 or later) |
| `shell_run_test` | shell: running commands, quoting, pipes, redirection |
| `shell_cd_test` | shell: `cd`, the virtual file system, wildcards |
| `shell_lists_test` | shell: `; && \|\|`, exit status, variables, `exit` |
| `shell_pipeline_test` | shell: the commands together with pipes, redirection and wildcards |
| `equiv_test` | **property test**: random inputs give the same results with LF, CR LF and mixed line endings (`WS_ITER`, default 8, `WS_ITER=100` for a longer run; `WS_SEED`) |
| `limits_test` | **limits**: sorting 500,000 lines, one line of 20 MB, sizes around the buffers (65536 +- 1), 2,000,000 empty lines, deep paths, many files (`WS_STRESS=1`: ten times larger) |
| `sharing_test` | **sharing violations**: `rm`/`mv`/`cp` of files and directories another process holds open, moves between volumes |
| `async_test` | **asynchronous**: input that comes in pieces, programs that wait for the end of their input, `tail -f` |

- `nmake test` ends with 0 when everything passes; a failure names the program, the location of the check and the expected and actual values.
- Test programs and work files are created in `test\out\` (not tracked by git). A single test can be run as `test\out\<name>.exe`.
- The test programs are run by `test\out\runall.exe`, several at a time (each has its own work directory, so they do not collide). A program that passed shows one summary line, a failed one all of its output. `WS_JOBS=N` sets how many run at once (default: half of the processors), `WS_VERBOSE=1` shows all output.
- **AddressSanitizer:** `nmake asan` rebuilds all tools and tests with `/fsanitize=address` (`bin-asan\`, `test\out-asan\`) and runs them. Memory errors are reported with their location and fail the test. The environment variable `WINSHELL_BIN` selects the directory of tools the tests use.
- **Comparison with GNU:** `test\gnu_compare.sh` compares `head`, `tail`, `wc`, `sort`, `uniq`, `cut`, `tr`, `cat`, `tac`, `nl`, `paste`, `tee`, `grep`, `sed`, `expr`, `find`, `xargs`, `basename`, `dirname`, `date`, `realpath` and `diff` with the GNU tools in over three thousand combinations of options and inputs (`LC_ALL=C`, `C.UTF-8` for grep). The script collects the combinations and `test\out\gnu_batch.exe` runs them several at a time (much faster than starting the programs from the MSYS bash: about 30 seconds). `GNU_ONLY=grep` runs one section, `GNU_IMMEDIATE=1` makes the script run them itself, one by one. What is meant to differ (a CR LF ending is not part of the line, `sort -z/-h/-M/-V` are refused, characters are UTF-8) is not compared.
- **`mv` to another drive:** with the environment variable `WINSHELL_TEST_CROSS_DEVICE` set, `mv` does not try the rename and takes the copy-and-remove path. The tests run that code on one volume with it, and for real when a second fixed drive that can be written to exists.
- When adding a command, add its cases to the `*_test.c` of its feature. The shared parts are `testlib.h` (checks, running programs) and `testtools.h` (work directory, running tools, fixtures). Output is compared without telling LF and CRLF apart (`lf_only`).
- Ctrl+C handling is not part of the automatic tests because it needs a real console.

## Layout

```
Winshell/
├─ Makefile        top-level build rules
├─ common.mk       build rules that every tool includes
├─ shell/          the shell itself (include/, src/)
├─ arch/           arch command
├─ basename/       basename command
├─ cat/            cat command
├─ cp/             cp command
├─ cut/            cut command
├─ echo/           echo command
├─ date/           date command
├─ diff/           diff command
├─ dirname/        dirname command
├─ env/            env command
├─ expr/           expr command
├─ find/           find command (parse.c parses the expression, eval.c walks and evaluates)
├─ grep/           grep command
├─ realpath/       realpath command
├─ sed/            sed command (script.c parses the script, exec.c runs it)
├─ sleep/          sleep command
├─ head/           head command
├─ kill/           kill command
├─ ls/             ls command
├─ mkdir/          mkdir command
├─ mv/             mv command
├─ printenv/       printenv command
├─ nl/             nl command
├─ paste/          paste command
├─ ps/             ps command
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
├─ which/          which command
├─ xargs/          xargs command
├─ common/         code shared by several commands (vpath: virtual path conversion, fsutil: copy and remove, textutil: text input and lines, args: option parser, regex: regular expressions, vec: growing arrays)
├─ test/           tests (nmake test)
└─ bin/            build output (not tracked by git)
```

## Adding a new command

1. Create a directory named after the command at the top level and write `main.c` in it. Handle the options with
   `args_parse` of `common/args.h` (`tee/main.c` is the smallest example: a table of options, a handler, one call).
2. Copy `tee/Makefile` and change `NAME`, `SRCS` and `HDRS` (and `INCLUDES`, `LIBS` if needed); the rules come from `common.mk`.
3. Add `build-<name>` to the `BUILD` list in the top-level `Makefile` (`clean-<name>` follows by itself).

`common.mk` provides the `all` and `clean` rules, and takes the output location through the `BINDIR` macro and extra compiler options through `EXTRA_CFLAGS`.

## Design goals

- **Virtual root directory:** like a UNIX filesystem there is a logical root directory `/`, and each drive is placed beneath it as a single lower case letter.
  - `C:\Users\me` is `/c/Users/me`. Only the drives are below `/`, and `cd /`, `pwd` and `cat /` all follow that.
  - Windows paths such as `C:\Users\me` keep working everywhere. UNC paths such as `\\server\share` are outside of the virtual tree and are passed to Windows unchanged.
  - `cd` is a builtin of the shell, which tells its children about the current directory through the environment variable `PWD`. While in `/`, the Windows directory is the root of the system drive, and relative paths do not refer to any file.
  - Every command converts the paths it receives itself. When a new command opens a file, use `vpath_to_native` from `common/vpath.h`.
- **UTF-8:** every program carries `common/utf8.manifest`, which makes it run in the UTF-8 code page: arguments, names of files, the environment and the input are UTF-8, and files with Korean names are handled as they are.
  - The shell reads the console with `ReadConsoleW` and puts the console code page into UTF-8 while it runs, giving it back at the end. The wildcards `?` and `[가-힣]` match one character, not one byte.
  - The manifest takes effect from Windows 10 version 1903. On an older Windows the programs run in the ANSI code page of the system, as before; ASCII names are the same everywhere. A tool run directly from `cmd` writes UTF-8, so `chcp 65001` is needed there.
- **Compatibility (goal):** use only basic system calls to stay compatible down to Windows XP. This has not been verified yet.

## Planned

**Shell features**: background jobs (`&`), brace expansion, command substitution (`$(...)`), `if`/`for`/`while`

**coreutils**

<details>
<summary>Show list</summary>

`[`, b2sum, base32, base64, basename, chgrp, chmod, chown, cksum, comm, csplit, date, dd, df, dir,
dircolors, dirname, du, expand, expr, factor, false, fmt, fold, groups, hostid, hostname, id, install,
join, link, ln, logname, md5sum, mkfifo, mknod, mktemp, nice, nohup, nproc, numfmt, od,
pathchk, pinky, pr, printf, ptx, readlink, realpath, seq, sha1sum, sha224sum, sha256sum,
sha384sum, sha512sum, shred, shuf, sleep, split, stat, stdbuf, sum, sync, test, true,
truncate, tsort, tty, uname, unexpand, unlink, uptime, users, vdir, who, whoami, yes

</details>

**Out of scope (meaningless or no counterpart on Windows)**: chcon, chroot, runcon, stty, coreutils (multi-call binary)

## License

[Apache License 2.0](LICENSE)
