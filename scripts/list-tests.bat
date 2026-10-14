@echo off
rem Lists the test programs of test\Makefile that need no special rule: every *_test.c in this
rem directory except the unit tests that link a part of the project (they have a rule of their
rem own in the Makefile). Writes one line,
rem   GENERIC_TESTS = $(OUT)\name_test.exe ...
rem which the Makefile includes (nmake runs this at the start, see "!IF [" in test\Makefile).
setlocal enabledelayedexpansion
set LIST=
for %%f in (*_test.c) do (
    set SKIP=
    for %%s in (vpath_test args_test regex_test parser_test) do if /i "%%~nf"=="%%s" set SKIP=1
    if not defined SKIP set LIST=!LIST! $(OUT^)\%%~nf.exe
)
echo GENERIC_TESTS =!LIST!
