@echo off
setlocal
cd /d "%~dp0"
where gcc >nul 2>&1 || (echo MinGW-w64 GCC is required. & exit /b 1)
gcc -std=c11 -O2 -Wall -Wextra -o orphan_decode.exe orphan_decode.c
exit /b %errorlevel%
