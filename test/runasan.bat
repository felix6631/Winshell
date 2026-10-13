@echo off
rem Runs the test programs that were built with AddressSanitizer (test\out-asan) against the programs
rem of the project that were built the same way (bin-asan).
setlocal
set WINSHELL_BIN=%~dp0..\bin-asan
"%~dp0out-asan\runall.exe" "%~dp0out-asan"
exit /b %errorlevel%
