@echo off
rem Runs every test program in the directory given as the first argument and reports which failed.
rem The exit code is 0 when all of them passed.
setlocal enabledelayedexpansion
set FAILED=
set COUNT=0
for %%t in ("%~1\*_test.exe") do (
    set /a COUNT+=1
    "%%t"
    if errorlevel 1 set FAILED=!FAILED! %%~nt
)
echo.
if defined FAILED (
    echo FAILED:%FAILED%
    exit /b 1
)
echo all !COUNT! test programs passed
exit /b 0
