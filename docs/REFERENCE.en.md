# Winshell reference

**English** | [한국어](REFERENCE.md) · [← README](../README.en.md)

The options of every command, the shell language, building and testing, and the internals.

## Implemented

| Command | Status |
| --- | --- |
| `shell` | Input loop, runs commands and waits for them, pipes/redirection, `; && \|\|`, variable expansion (`$? $0 $1..$9 $# $@ $*`), command substitution (`$(...)`, backticks), comments (`#`), control structures (`if`/`elif`/`else`, `while`, `until`, `for`, `case`, `break`, `continue`, `:`), functions (`name() { ...; }`, `return`, `local`), groups (`{ ...; }`, `( ... )`), `trap` (`EXIT` `INT` `ERR`), arithmetic expansion (`$((...))`), parameter expansion (`${NAME:-value}` and more), `read`, redirections and pipes of loops, wildcards, running scripts (`shell FILE [ARG...]`, `shell -c`, `source`/`.`), `cd` `export` `unset` `exit` `source` |
| `arch` | Done |
| `basename` | Done (`-a -s -z`; both `/` and `\` separate; the suffix is removed unless it is the whole name) |
| `date` | Done (`-d -u -r -I -R --rfc-3339`, the format letters `%a %A %b %B %c %C %d %D %e %F %g %G %H %I %j %k %l %m %M %N %p %P %q %r %R %s %S %T %u %U %V %w %W %x %X %y %Y %z %:z %Z` with the flags `- _ 0 ^ #` and a width; `-d` reads `@SECONDS`, ISO dates and times, month names, `now yesterday tomorrow`, `+2 days`, `3 weeks ago`, `next month`, weekdays; years before 1970 and after 3000 work; `-s` (setting the clock) is not supported) |
| `diff` | Done (normal / `-u -U N` / `-c -C N` / `-NUM`, `-q -s -i -w -b -B -a -r -N -x --label --strip-trailing-cr`; the algorithm of GNU diff (Myers middle snake, the common ends, the shifting of runs, the heuristics for big inputs) so that the same one of several equal results is picked; directories; exit status 0/1/2; not there: `-y -e -p --color` and more) |
| `dirname` | Done (`-z`; several names) |
| `cat` | Done (`-A -b -e -E -n -s -t -T -u -v`, `--help`, `--version`) |
| `comm` | Done (`-1 -2 -3`, `--check-order`, `--nocheck-order`, `--output-delimiter`, `--total`, `-z`; byte comparison (C locale), a CR LF is a line ending and a line is written as it was read; an order violation is reported once as GNU does, status 1) |
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
| `test_cmd` | Done (installed as `test` and `[`: strings `= == != < >`, integers `-eq -ne -lt -le -gt -ge`, files `-e -f -d -s -r -w -x -L -h -c -nt -ot -ef -O -G -N -t`, `! ( ) -a -o`; as in GNU one to four arguments are read by their number; `[` wants its `]`; exit status 0/1/2; `-r -w -x` mean can be opened / not read-only / a directory or a PATHEXT extension on Windows) |
| `true` | Done (ignores its arguments, 0) |
| `false` | Done (ignores its arguments, 1) |
| `tr` | Done (`-c -C -d -s -t`, classes `[:alpha:]` etc.) |
| `uniq` | Done (`-c -d -D -u -i -f -s -w`) |
| `wc` | Done (`-l -w -m -c -L`) |
| `printf` | Done (GNU printf: `%d %i %o %u %x %X %e %f %g %a %c %s %b %%`, flags/width/precision/`*`, escapes `\n \xHH \NNN \uHHHH \c`, the format is reused, `'a` character constants; numbers are `double`, not `long double`) |

For what is still to come, see the [roadmap in the README](../README.en.md#roadmap).

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
  - Brace expansion is done before the other expansions on the words outside quotes: `a{b,c}d` -> `abd acd`, `{1..5}`, `{5..1}`, `{01..10}`
    (a leading 0 pads), `{a..e}`, `{1..10..3}` (a step), nested `x{a,b{1,2}}y`, several `{a,b}{1,2}` -> `a1 a2 b1 b2`. It is an expression
    only with a comma at its top level or a range of two numbers or two letters; `{}`, `{a}`, `{ list; }` and `find -exec {}` stay. Not
    expanded: quotes, `${...}` and `$(...)`, the value of `NAME={a,b}`, the target of a redirection (`> out{1,2}`), the patterns of a
    `case`, and `\{` (the `\` stays). A word that would become more than 65536 stays as it is.
  - `list &` runs a whole list joined with `&&`/`||` as a background job and goes on at once (status 0). `$!` is the process id of the last
    job, `$$` the process id of the shell. A pipeline of programs is started without waiting; builtins, functions, control structures and
    groups run in a `shell -c` of their own, which is given the variables that are not exported, the functions and the positional
    parameters (changes made afterwards are not seen by the other). The standard input of a job is NUL and Ctrl+C does not reach it.
    `jobs` lists the jobs (`[1]+  Running   sleep 5 &`); `wait [PID|%N]...` waits and returns the status of the last one (127 for an unknown
    PID). On a terminal `[1] PID` is printed when a job starts, and a job that ended is reported as `[1]+  Done   command` before the next
    prompt. Jobs go on after the shell has ended.
  - A job is named `%N` (or `N`), `%%`/`%+` (the current one: the last started), `%-` (the one before), `%string` (its command starts
    with it) or `%?string` (has it). `fg [job]` shows the command, waits for the job and returns its status (a Ctrl+C while waiting ends
    the job, 130). `bg [job]` lets a job that `kill -STOP` stopped go on (`[1]+ command &`), and says "already in background" when it runs.
    `jobs` shows a stopped job as `Stopped` and has `-l` (with the PID) and `-p` (PIDs only). `disown [job|-a]` forgets jobs without ending
    them. A `%job` argument of `kill` is replaced by the process ids of every process of the job (`kill -9 %1`, `kill %+ %-`). An unknown job
    is a message and status 1. A Windows console has no Ctrl+Z that stops a job: use `kill -STOP`. A job gets NUL as its input, so `fg`
    does not give it the terminal.
- **Scripts and command substitution**
  - `shell FILE [argument...]` runs the lines of the file one after the other (`$0` is FILE, `$1`..`$9`, `$#`, `$@`, `$*` are the
    arguments); so does `shell -c 'command' [name [argument...]]`. There may be any number of arguments (`${10}` works). `"$@"` becomes one
    word per argument (`"x$@y"` joins the text around it to the first and the last, and with no arguments it is no word), `"$*"` is one word
    joined with spaces, and an unquoted `$@` is split at blanks. `for x; do` goes over `"$@"`. `shift [N]` drops the first N (status 1 when
    there are fewer). A function, a subshell and `$(...)` put the arguments back when they end. `source FILE [argument...]` and `. FILE` run a file in the
    same shell; without arguments it sees the `$1`.. of the caller. In a file `exit [N]` ends the shell, and the status of a script
    is that of its last command (or of `exit`). A line that is a syntax error is reported and the next one runs. LF and CR LF line
    ends and a leading UTF-8 BOM are accepted.
  - `$(command)` and `` `command` `` are replaced by the standard output of the command, without the line ends at its end.
    Outside of quotes it is cut at blanks into words, inside of quotes it is one word. They nest, and may hold `;`, `&&` and
    quotes. Inside single quotes they are text. As in a subshell, the current directory and the variables (the environment as
    well) are what they were when the substitution has ended, and an `exit` inside ends only the substitution. For a line that is
    only `x=$(command)`, `$?` is the status of the command; otherwise `$?` around a substitution is not changed.
  - A `#` at the start of a word (outside of quotes) makes the rest of the line a comment.
- **Arithmetic, parameter expansion, read**
  - `$(( expression ))`: 64-bit integers, the operators of C (`+ - * / % ** << >> & | ^ ~ ! && || ?: , < <= > >= == !=` and the
    assignments `= += -= ...`), decimal, `0x` and `0` octal, variables by name (an unset one is 0). Dividing by 0 is an error and the
    command does not run. The branch that is not taken is not computed.
  - `${NAME:-w}` `${NAME-w}` `${NAME:=w}` `${NAME:+w}` `${NAME:?w}`, `${#NAME}`, `${NAME#p}` `${NAME##p}` `${NAME%p}` `${NAME%%p}`,
    `${NAME:offset}` and `${NAME:offset:length}`. The word and the pattern are expanded like the arguments of a command, a pattern uses
    `* ? [..]`. With the `:` an empty value counts as not set.
  - `${NAME/pattern/string}` replaces the first match of the pattern (the one that starts first, the longest of those) with the string;
    `${NAME//pattern/string}` replaces all of them, `${NAME/#pattern/string}` only a match at the start, `${NAME/%pattern/string}` only
    one at the end, and without `/string` the match is deleted. A `/` in the pattern is written `\/`. Variables and substitutions are
    expanded in the pattern and the string, blanks and `| < > ;` are plain characters, braces are not expanded, and a `&` in the string
    is not special.
  - `read [-r] [-p prompt] [name...]`: a line of the input is cut at `IFS` (blanks by default) into the variables, the last one gets the
    rest; without a name it is `REPLY`. Without `-r` a backslash protects the next character and one at the end joins the next line. Status
    1 at the end of the input. At a terminal the `-p` prompt is shown. `NAME=value` in front of a builtin (`IFS=: read a b`) holds for it only.
- **Control structures**
  - `if LIST; then LIST; [elif LIST; then LIST;]... [else LIST;] fi`, `while LIST; do LIST; done`, `until LIST; do LIST; done`,
    `for NAME [in WORD...]; do LIST; done`, and `break [N]`, `continue [N]`, `:`. A reserved word is one only as the first word of a
    command and unquoted; a newline may stand where `;` does. The words of `for` are expanded like the arguments of a command
    (variables, substitutions, wildcards); without `in` it goes over `$1`..`$9`. The status of an `if` is that of the branch that ran
    (0 when none did), that of a loop is the status of its last round.
  - A compound command stands where a command stands in a `&&`/`||` list (`if ...; fi && echo`). Redirections may follow its end:
    `while read a b; do ...; done < file`, `for ...; do ...; done > out 2>&1`, `fi >> log`. They are applied to the standard
    input and output of the shell itself, so the builtins and the programs run inside read and write there.
  - A compound command, a `{ ...; }` or `( ... )` group, a function or a builtin may be a stage of a pipe: `cmd | while read x; do ...; done`,
    `for ...; done | sort`, `printf 'a\n' | f | tr a-z A-Z`. With such a stage the stages run one after the other: only the last one runs in
    the current shell (its assignments stay, and `echo v | read w` works); the others run to their end in a subshell and their output is the
    input of the next stage. It is not streamed, so a stage that never ends (`yes | f`) does not work. The status is that of the last stage.
    A pipeline of programs only is still streamed.
  - `case WORD in PATTERN[|PATTERN]...) LIST ;; ... esac`: the word and the patterns are expanded like the arguments of a command
    (variables, substitutions, quotes) and a pattern uses `* ? [..]`. A `*` in quotes is plain, and only the first clause that matches runs.
    The status is 0 when none matches; the `;;` of the last clause may be left out.
  - Functions: `name() compound-command` (`name() { ...; }`, `name() if ...; fi`, ...). A definition stays for the session, a new one with the
    same name replaces it, `unset -f name` removes it. A call makes the arguments `$1`..`$9`, `$#`, `$@` and `$*` and puts the caller's back
    when it ends (`$0` is not changed). `return [N]` ends the function (without N with the status of the last command); `local NAME[=value]`
    makes a variable the function's own, put back when it ends. A function runs in the same shell, so its assignments stay; a call may be in a
    `&&`/`||` list, have redirections (`f > file`), be in a substitution (`$(f x)` ) and in a loop. The nesting is limited to 200 levels.
  - `{ LIST; }` groups a list into one command in the current shell (it may have redirections and be on the right of a pipe); `( LIST )` is a
    subshell: the directory and the variables are as they were when it ends, and an `exit` inside ends only the subshell. `{` and `}` are
    reserved words only as the first word of a command.
  - `trap [ACTION] SIGNAL...` handles `EXIT` (or 0), `INT` (2, Ctrl+C) and `ERR`. ACTION is a string of commands, `''` ignores the signal,
    `-` puts the default back. `trap` or `trap -p` shows what is set as `trap -- 'ACTION' NAME`. `EXIT` runs once when the shell ends (end of
    the script, `exit`), and an `exit N` inside it gives the status of the shell. `INT` stops the command that was running with Ctrl+C, runs
    the trap and the script goes on (without a trap a loop stops). `ERR` runs after a simple command failed, but not in a condition
    (`if`, `while`), on the left of `&&`/`||` and inside functions. A subshell and `$(...)` start without traps (what was ignored stays
    ignored). The other signals (`HUP`, `TERM`, ...) do not exist on Windows: "signal not supported".
  - The input is read one command of the top level at a time (up to the next newline) and parsed whole before it runs, so a line with a
    syntax error runs nothing; in a script file the commands before the error have run when it ends with status 2. At a terminal an
    unfinished `if`/`while`/`for`, an open quote, or a line that ends in `&&`, `||` or `$(` goes on in the next line (prompt `> `). Ctrl+C
    in a loop stops it at the end of the round (status 130).
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

- The top-level `Makefile` creates `bin\` and then invokes the `Makefile` of each sub-project (`shell`, `arch`, `basename`, `cat`, `comm`, `cp`, `cut`, `date`, `diff`, `dirname`, `echo`, `env`, `expr`, `false`, `find`, `grep`, `head`, `kill`, `ls`, `mkdir`, `mv`, `nl`, `paste`, `printenv`, `printf`, `ps`, `pwd`, `realpath`, `rm`, `rmdir`, `sed`, `sleep`, `sort`, `tac`, `tail`, `tee`, `test_cmd`, `touch`, `tr`, `true`, `uniq`, `wc`, `which`, `xargs`).
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
| `comm_test` | `comm`: columns, suppressing them, CR LF, order check, `--total`, delimiter, standard input, exit status |
| `realpath_test` | `realpath`: virtual paths, what has to exist, relative paths, junctions |
| `names_test` | `basename`, `dirname`: separators (also `\`), suffixes, NUL endings, the root |
| `xargs_test` | `xargs`: batches, how items are read, files, exit status, parallel runs |
| `find_test` | `find`: order of the walk, depth, `-prune`, `-exec`/`-execdir`/`-ok`, `-delete`, virtual paths, Korean names, exit status |
| `expr_test` | `expr`: arithmetic, comparison, logic, string functions, big integers, exit status |
| `sed_test` | `sed`: substitution, addresses, commands, `-i` and backups, `w`/`r` files, CR LF and the end of the last line, exit status, UTF-8 |
| `grep_test` | `grep`: CR LF, UTF-8, binary files, `-r`, context, exit status, input that comes as it is written |
| `shell_utf8_test` | shell: Korean names and directories, wildcards that match a character, variables, redirection (Windows 10 1903 or later) |
| `shell_run_test` | shell: running commands, quoting, pipes, redirection |
| `shell_script_test` | shell scripts: file/`-c`/`source`, `$0 $1.. $# $@`, command substitution (`$(...)`, backticks), comments, exit status |
| `shell_control_test` | shell control structures: `if`/`elif`/`else`, `while`, `until`, `for`, `break`/`continue`, several lines, syntax errors |
| `test_cmd_test` | `test`, `[`, `true`, `false`: strings, integers, files, expressions, the rules of the number of arguments, errors |
| `shell_cd_test` | shell: `cd`, the virtual file system, wildcards |
| `shell_lists_test` | shell: `; && \|\|`, exit status, variables, `exit` |
| `shell_pipeline_test` | shell: the commands together with pipes, redirection and wildcards |
| `equiv_test` | **property test**: random inputs give the same results with LF, CR LF and mixed line endings (`WS_ITER`, default 8, `WS_ITER=100` for a longer run; `WS_SEED`) |
| `limits_test` | **limits**: sorting 500,000 lines, one line of 20 MB, sizes around the buffers (65536 +- 1), 2,000,000 empty lines, deep paths, many files (`WS_STRESS=1`: ten times larger) |
| `sharing_test` | **sharing violations**: `rm`/`mv`/`cp` of files and directories another process holds open, moves between volumes |
| `async_test` | **asynchronous**: input that comes in pieces, programs that wait for the end of their input, `tail -f` |
| `printf_test` | `printf`: escapes, conversions and flags, reuse of the format, character constants, status after numeric errors |

- `nmake test` ends with 0 when everything passes; a failure names the program, the location of the check and the expected and actual values.
- Test programs and work files are created in `test\out\` (not tracked by git). A single test can be run as `test\out\<name>.exe`.
- The test programs are run by `test\out\runall.exe`, several at a time (each has its own work directory, so they do not collide). A program that passed shows one summary line, a failed one all of its output. `WS_JOBS=N` sets how many run at once (default: half of the processors), `WS_VERBOSE=1` shows all output.
- **AddressSanitizer:** `nmake asan` rebuilds all tools and tests with `/fsanitize=address` (`bin-asan\`, `test\out-asan\`) and runs them. Memory errors are reported with their location and fail the test. The environment variable `WINSHELL_BIN` selects the directory of tools the tests use.
- **Comparison with GNU:** `test\gnu_compare.sh` compares `head`, `tail`, `wc`, `sort`, `uniq`, `cut`, `tr`, `cat`, `tac`, `nl`, `paste`, `tee`, `grep`, `sed`, `expr`, `find`, `xargs`, `basename`, `dirname`, `date`, `realpath`, `diff`, `comm`, `test`, `true` and `false` with the GNU tools in over three thousand combinations of options and inputs (`LC_ALL=C`, `C.UTF-8` for grep). The script collects the combinations and `test\out\gnu_batch.exe` runs them several at a time (much faster than starting the programs from the MSYS bash: about 30 seconds). `GNU_ONLY=grep` runs one section, `GNU_IMMEDIATE=1` makes the script run them itself, one by one. What is meant to differ (a CR LF ending is not part of the line, `sort -z/-h/-M/-V` are refused, characters are UTF-8) is not compared.
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
├─ comm/           comm command
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
├─ false/        false command
├─ test_cmd/     test_cmd command
├─ true/         true command
├─ printf/       printf command
├─ common/         code shared by several commands (vpath: virtual path conversion, fsutil: copy and remove, textutil: text input and lines, args: option parser, regex: regular expressions, vec: growing arrays)
├─ test/           tests (nmake test)
└─ bin/            build output (not tracked by git)
```

## Adding a new command

1. Run `nmake new NAME=<name>`. It creates a working skeleton (`<name>\main.c`, `Makefile`), `test\<name>_test.c` and
   `test\gnu\<name>.sh`, and adds entries to the command table, the test table and the directory tree of both references (`docs\REFERENCE.md`, `docs\REFERENCE.en.md`).
2. Write `main.c`. Handle the options with `args_parse` of `common/args.h` (`tee/main.c` is the smallest example). The
   `Makefile` of a tool only sets `NAME`, `SRCS` and `HDRS` (and `INCLUDES`, `LIBS` if needed); the rules come from `common.mk`.
3. Fill in the row of the command table (and add the name to the "Commands" groups of the README), and write the tests and the GNU comparison (`test\gnu\<name>.sh`).
   `nmake check-registry` points at a tool that is missing from a reference or from the tests.

The build list, the test list and the GNU comparison list are made from the files, so nothing has to be added to them: every
directory with a `Makefile` is a tool (`scripts\list-tools.bat`), every `test\*_test.c` is a test program
(`scripts\list-tests.bat`; only the unit tests that link other sources have a rule in `test\Makefile`), and every
`test\gnu\*.sh` is a section of the GNU comparison. `nmake sync-readme` brings the directory tree and the sentence about
the build structure in line with the directories.

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
