@echo off
rem Runs the test programs that were built with AddressSanitizer (test\out-asan) against the programs
rem of the project that were built the same way (bin-asan).
setlocal
set WINSHELL_BIN=%~dp0..\bin-asan
call "%~dp0runall.bat" "%~dp0out-asan"
exit /b %errorlevel%
