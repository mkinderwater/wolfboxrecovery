@echo off
setlocal
cd /d "%~dp0"
set "TOOLBIN="
where gcc >nul 2>&1
if not errorlevel 1 for %%I in (gcc.exe) do set "CC=gcc"
if not defined CC if exist "C:\msys64\ucrt64\bin\gcc.exe" (
 set "CC=C:\msys64\ucrt64\bin\gcc.exe"
 set "TOOLBIN=C:\msys64\ucrt64\bin\"
)
if not defined CC (echo MinGW-w64 GCC is required.& exit /b 1)
set "WINDRES=windres"
where windres >nul 2>&1
if errorlevel 1 if defined TOOLBIN set "WINDRES=%TOOLBIN%windres.exe"
"%WINDRES%" --version >nul 2>&1
if errorlevel 1 (echo MinGW-w64 windres is required to embed the icon and manifest.& exit /b 1)
"%WINDRES%" -i WolfBoxRecovery.rc -o WolfBoxRecovery.res -O coff
if errorlevel 1 exit /b 1
"%CC%" -std=c11 -O2 -Wall -Wextra -mwindows -o WolfBoxRecovery.exe win_gui.c recovery_core.c jpeg_carve.c orphan_hevc.c sample_tables.c ownership.c unit_runs.c video_export.c mp4_export.c audio_mapping.c fat_geometry.c scan_cache.c WolfBoxRecovery.res -lcomdlg32 -lcomctl32 -lshell32 -lole32 -lgdi32 -luser32
if errorlevel 1 exit /b 1
echo Built WolfBoxRecovery.exe with embedded icon and application manifest.
