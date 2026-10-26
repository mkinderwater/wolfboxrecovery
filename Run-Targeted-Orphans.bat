@echo off
setlocal
cd /d "%~dp0"
if "%~1"=="" (echo Usage: Run-Targeted-Orphans.bat "disk-image" "orphan_hevc_samples.csv" "orphan_units.csv" [output-dir] & exit /b 2)
if "%~2"=="" (echo Missing sample manifest & exit /b 2)
if "%~3"=="" (echo Missing unit manifest & exit /b 2)
set "OUT=%~4"
if not defined OUT set "OUT=Orphan_Output"
if not exist orphan_decode.exe call Build-Targeted-Windows.bat
if not exist orphan_decode.exe exit /b 3
orphan_decode.exe "%~1" "%~2" "%~3" "%OUT%"
exit /b %errorlevel%
