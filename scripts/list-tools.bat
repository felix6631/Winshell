@echo off
rem Lists the tools for the top-level Makefile: every directory next to it that has a Makefile
rem (test excluded) is a tool, so a new command needs no entry in a list. Writes one line,
rem   BUILD = build-<name> build-<name> ...
rem which the Makefile includes (nmake runs this at the start, see "!IF [" in Makefile).
setlocal enabledelayedexpansion
set LIST=
for /d %%d in (*) do (
    if exist "%%d\Makefile" if /i not "%%d"=="test" set LIST=!LIST! build-%%d
)
echo BUILD =!LIST!
