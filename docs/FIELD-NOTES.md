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

* `/dev/urandom`, `/dev/zero`, `/dev/stdin`, `/dev/tty` do not exist (only `/dev/null` does). `head -c 16 /dev/urandom` fails.
* `echo $HOME` is what the environment says (a Windows path when the parent put one there); `~` and `cd ~` give `/c/Users/...`.
* A script that needs `chmod +x` to run is run anyway (there is no executable bit); `chmod +x` is accepted.
* `$$`, `$RANDOM`: `$$` is the id of the process of Windows; `$RANDOM` is not set.
* Pipelines that start programs of Windows with a console (`more`, `choice`) behave as on Windows.
* The 9 patterns in `test/shell/cases.txt` that still differ from bash (see HARNESS-2026-11-10.md).
* Paths are shown as `/c/Users/...` by `pwd` and `realpath`; programs of Windows get what the script gave them
  (`python a.py` is fine; a script that passes `/c/...` to a Windows program that knows no such path needs `cygpath`-like care:
  the shell does not rewrite arguments).

## How to add to this page

Run the commands your agent sends, note the ones whose output or status you would not accept from bash, reduce each to a few
lines, and put the reduced case in `test/shell/cases.txt` (`=== name`, then the commands) so `bash test/shell_compare.sh` keeps
watching it.
