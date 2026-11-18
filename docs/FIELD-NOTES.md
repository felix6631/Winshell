# Field notes (2026-11-16)

What happened when the shell was used the way an AI agent uses it: `shell.exe -c "<command line>"` on this repository and on
scratch directories, with the commands such an agent sends (git, grep -rn, find, sed -i, heredocs that write files, python and
node, xargs, pipes, loops, redirections, timeouts). The method: write 60 or so command lines, run them, read the output, and fix
what a person who knows bash would call wrong. Each line below is a finding; "fixed" says where.

## Worked as it is

git (status, log, diff, init, add, commit, grep), `grep -rn --include`, `find -exec ... {} +`, `sed -i`, `sed -n`, `awk`, `xargs`,
`cp -r` and `diff -r`, `tee`, `tr`, `sort | uniq -c | sort -rn`, `wc`, `head`/`tail`, `stat -c`, `realpath`, `basename`/`dirname`,
here documents into files and into `python -`, here-strings, process substitution, `for` over globs, `$(...)`, `set -e`,
`timeout`, `wait`, `export` and `VAR=x cmd`, UTF-8 (Korean) in file names and contents, `python`, `node`, `npm`, `curl`, `tar`
(found in PATH), the exit codes of failing commands.

## Fixed in this round

| Finding | Fix |
| --- | --- |
| `cp x /tmp/y` and `mkdir /tmp/z` failed: there is no `/tmp` | `/tmp` and `/var/tmp` are the temporary directory of the user (`common/vpath.c`) |
| `nmake /?`, `cmd /c` were run with `/c` or `/d`: the glob `/?` matched the drives of the virtual root | `/?` is not expanded (`shell/src/glob.c`) |
| `> file` alone (to empty a file) was a syntax error | a command that is only redirections is run (`shell/src/parser.c`) |
| `yes \| head -1` printed `yes: standard output: Broken pipe` | `yes` ends silently with 141, as when GNU yes gets SIGPIPE |
| `./script.sh` with `#!/bin/bash`, `#!/usr/bin/env python3`, and scripts without a first line could not be run | a script is run by its interpreter (our shell for sh, bash, dash, zsh; the program found in PATH otherwise); a file with no extension is found in PATH last (`shell/src/executer.c`, `common/program.c`) |
| `export PATH="$PWD/bin:$PATH"` made a list that neither Windows nor the shell could read (a colon between directories, /c/dir) | PATH is turned into the Windows form (`;` and `C:\dir`) when it is assigned or exported, as MSYS does (`shell/src/variables.c`) |

## Known, not fixed

* `/dev/stdin` and `/dev/tty` cannot be opened by the tools (`cat /dev/stdin` fails; write `cat` or `cat -`). In a redirection of the shell
  (`< /dev/stdin`, `> /dev/stderr`, `> /dev/stdout`) they work.
* `/dev/zero` is a sparse file of 1 GiB and `/dev/urandom` a file of 4 MiB of fresh random bytes (made in the temporary directory at
  the first use of a process); a program that reads more gets an end of file.
* `echo $HOME` is what the environment says (a Windows path when the parent put one there); `~` and `cd ~` give `/c/Users/...`.
* A script that needs `chmod +x` to run is run anyway (there is no executable bit); `chmod +x` is accepted.
* `$$` is the id of the process of Windows. `time` reports the real time only (`user` and `sys` are 0).
* `$OSTYPE` is `msys`, `$BASH_VERSION` says 5.2.21(1)-winshell, `$UID` is the number that `id -u` shows.
* `umask` and `ulimit` keep and show values, they change nothing.
* Pipelines that start programs of Windows with a console (`more`, `choice`) behave as on Windows.
* `test/shell/cases.txt` has no case that differs from bash (231 of them).
* Paths are shown as `/c/Users/...` by `pwd` and `realpath`; programs of Windows get what the script gave them
  (`python a.py` is fine; a script that passes `/c/...` to a Windows program that knows no such path needs `cygpath`-like care:
  the shell does not rewrite arguments).

## Round 2 (2026-11-18)

110 more command lines, run by `shell.exe` and by bash (`CASES=file bash test/shell_compare.sh`); 20 differed. All but the ones
below were real gaps and are fixed (the cases are in `test/shell/cases.txt` as `r2_*`).

| Finding | Fix |
| --- | --- |
| `echo a \| { read x; exit 2; }; echo $?` ended the whole shell: an `exit` in the last stage of a pipeline was an `exit` of the shell | the stage is a subshell for `exit` too (`shell/src/command.c`) |
| `cmd \| (subshell)` was a "Failed to execute command '(exit'" | a `(` after `\|` starts a compound stage |
| `IFS=,; set -- $v` did not split at `,` (only at blanks) | word splitting follows IFS: blanks around a field go, other characters end a field, `IFS=` splits nothing (`shell/src/parser.c`) |
| `$(<file)` gave nothing | it is `$(cat <file)` |
| `declare -A m=([a]=1 [b]=2)`, `a=([0]=x [5]=y)`, `local -a arr=(...)` and `local -A` | the declaration and the array are two steps; `[key]=value` elements (`shell/src/interp.c`) |
| `$((16#ff))`, `$((2#101))` | `BASE#NUMBER` in arithmetic (bases 2 to 64) |
| `$'it\'s'` was an unterminated quote; `$'\u00e9'` stayed as it was | a backslash inside `$'...'` protects the next character; `\u` and `\U` are written as UTF-8 |
| `${x:?msg}` ended a command with status 2 and went on | it ends the script with status 1, as `set -u` does |
| no `mapfile`/`readarray`, `printf -v`, `builtin`, `umask`, `ulimit`, `time` | added (`mapfile -t -n -s -O`, `printf -v NAME`, `time [-p] pipeline` prints real time) |
| `$RANDOM`, `$SECONDS`, `$BASH_VERSION`, `$HOSTNAME`, `$UID`, `$EUID`, `$OSTYPE`, `$LINENO`, `${FUNCNAME[@]}`, `${PIPESTATUS[@]}` were empty | made up as bash does (`$LINENO` is the line of the command in its file or text) |
| `head -c 16 /dev/urandom \| base64`, `dd if=/dev/zero ...` failed: no such file | `/dev/zero`, `/dev/urandom` and `/dev/random` stand for files of the temporary directory (`common/vpath.c`); `> /dev/stderr` works in the shell |
| a redirection that could not be made gave status 127 (`command not found`) | status 1, as in bash |

Not fixed: `cd -` prints the path as `/c/Users/.../Temp/...` where bash with MSYS prints `/tmp/...` (paths are shown in the form of the
drive); Windows `python` writes CRLF.

## How to add to this page

Run the commands your agent sends, note the ones whose output or status you would not accept from bash, reduce each to a few
lines, and put the reduced case in `test/shell/cases.txt` (`=== name`, then the commands) so `bash test/shell_compare.sh` keeps
watching it.
