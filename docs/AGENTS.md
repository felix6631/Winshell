# Winshell for AI agents

Winshell is a bash-style shell and GNU-style coreutils for Windows, written in C on the Win32 API. It is meant to be what an
agent runs its shell commands with instead of a POSIX emulator (MSYS2, Cygwin, WSL): the same commands, the same output bytes
and exit codes as GNU, without an emulation layer. This page is the short version of what an agent needs to know. The full
list of options is in [REFERENCE.en.md](REFERENCE.en.md).

## Running commands

```sh
shell.exe -c 'grep -rn TODO src | head -5'      # one command line; the exit status is the status of the last command
shell.exe -lc '...'  /  -ec  /  -euo pipefail -c '...'   # bash-style option letters are read (-l -i -x -v ... do nothing)
shell.exe script.sh arg1 arg2                   # a file; $0 is the file, $1 $2 the arguments
echo 'echo hi' | shell.exe                      # commands from standard input (no prompt when it is not a console)
shell.exe -s a b < script.sh                    # the same, with $1=a $2=b
```

* Standard output and standard error stay separate; the exit status is the one of the last command, 127 for a command that
  was not found, 2 for a syntax error or a bad option. Output has LF line ends; input with CR LF is read as lines.
* Everything is UTF-8 (arguments, file names, input, output) on Windows 10 1903 or later.
* A command is looked for in the `bin\` next to `shell.exe` first (so `sort`, `find` and `timeout` are the GNU-style ones, not the
  Windows ones), then in `PATH` (`git`, `python`, `node`, `npm` and other `.cmd` and `.bat` files work).

## Paths

* The root `/` holds the drives: `C:\Users\me` is `/c/Users/me`. Both forms work everywhere, in arguments and in redirections.
* `/dev/null` is the null device. There are no other device files, no `/proc`, no `/tmp` (use `$TEMP` or `mktemp`).
* A backslash is an escape for the characters that need one (`\ ` `\;` `\$` `\"` `\'` ...) and stays a backslash in front of a letter, a
  digit, a wildcard or another backslash, so `C:\Users\me`, `dir\*.txt` and `\\server\share` mean what they say.

## What the shell understands

Lists (`;` `&&` `||` `!`), pipes (every stage is a subshell, as in bash), `if` `while` `until` `for` `for ((;;))` `case` `select`,
functions (`name()` and `function name`), `$(...)` and backticks, `$((...))` and `(( ))`, `[ ]` and `[[ ]]` (patterns, `=~` with
`BASH_REMATCH`, file tests), here documents (`<<EOF`, `<<-`, `<<'EOF'`), here-strings, `<(command)`, `$'...'`, brace expansion,
`${var:-x}` and the other expansions (`# ## % %% / // ^^ ,, : !name`), arrays, `trap` (EXIT, ERR, INT), `set -e -u -o pipefail`,
`eval`, `exec COMMAND`, `source` (and `return` in it), `getopts`, `read`, `readonly`, `declare` (`-r -x -i -A`), associative arrays,
`${@:N}`, `"$*"` with IFS, `case` with `;&` and `;;&`, `type`, `command -v`, background jobs (`&`, `wait`, `jobs`).

Not done (yet): file descriptors above 2 (`exec 3>file`, `>&3`), `printf %q` (the `printf` program is GNU's, which has no `%q`),
`shopt` options (accepted, nothing happens), glob order as the C locale has it (`B.txt a.txt`; here case is ignored), Ctrl+Z job
control, `set -x` output, signals other than INT in `trap`.
Run `bash test/shell_compare.sh` (needs MSYS bash) to see what still differs from bash on the patterns in `test/shell/cases.txt`.

## Commands

All of GNU coreutils that has a meaning on Windows (see the table in the README), plus `grep` `sed` `find` `xargs` `diff` `ps` `kill`
`which` `env`. The output of 90+ of them is compared byte by byte with GNU by `nmake gnu`. What is different on purpose:

* Sorting and `ls` order ignore case (the Windows way), so `ls` lists `a.txt B.txt`, where GNU in the C locale lists `B.txt a.txt`.
* There is no owner, group, mode or link semantics beyond what Windows has: `ls -l` makes a mode from the attributes, `chmod` and
  `chown` do what they can, `mkfifo` `mknod` `stdbuf` check their options and then refuse, `stty` knows only `size`, `isig`, `icanon`,
  `echo` on a console, `ls -Z` always shows `?`.
* `kill` and `timeout` map signals to what Windows can do (`KILL` ends the process, other signals close its windows or end it).
* `who` `users` `uptime` `pinky` list the sessions of Winshell shells, not of the whole machine.

## When something fails

1. Read the message: the tools write GNU-style messages (`ls: cannot access 'x': No such file or directory`).
2. A command that is not found is status 127 with `Error: Failed to execute command 'x'`: check `command -v x` and `PATH`.
3. The shell is plain text in, plain text out: if a script behaves differently from bash, reduce it to a few lines and add it to
   `test/shell/cases.txt` (a case starts with `=== name`); the harness will show the difference.

## Installing

```powershell
nmake                                   # build (Visual Studio Build Tools); a release zip has bin\ ready
powershell -File scripts\install.ps1    # adds bin\ to the PATH of the user (-Remove takes it out) and prints the path of shell.exe
```

Point the tool that runs shell commands at `bin\shell.exe` (`shell.exe -c "<command>"` is the form it needs).
