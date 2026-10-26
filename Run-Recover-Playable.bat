@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
set "OUT=%~1"
if not defined OUT if defined WOLFBOX_ORPHAN_OUTPUT set "OUT=%WOLFBOX_ORPHAN_OUTPUT%"
if not defined OUT set "OUT=Orphan_Output"
set "FFMPEG=ffmpeg"
set "FFPROBE=ffprobe"
if defined WOLFBOX_FFMPEG set "FFMPEG=%WOLFBOX_FFMPEG%"
if defined WOLFBOX_FFPROBE set "FFPROBE=%WOLFBOX_FFPROBE%"
if not exist "%OUT%" (echo No orphan candidates in "%OUT%" & exit /b 2)
"%FFMPEG%" -version >nul 2>&1
if errorlevel 1 (echo FFmpeg missing: set WOLFBOX_FFMPEG to ffmpeg.exe or put it on PATH. & exit /b 3)
set "FPS=%~2"
if not defined FPS if defined WOLFBOX_FPS set "FPS=%WOLFBOX_FPS%"
if not defined FPS (echo Usage: Run-Recover-Playable.bat "Orphan_Output" frame_rate & echo A supported frame rate is required; do not assume 25 fps for other devices. & exit /b 4)
set /a count=0
for %%F in ("%OUT%\candidate_*.hevc") do if exist "%%~fF" (
 set /a count+=1
 "%FFMPEG%" -hide_banner -nostdin -v warning -xerror -i "%%~fF" -map 0:v:0 -f null NUL 2>"%%~dpnF.decode.log"
 if errorlevel 1 (
   echo Decode failed: %%~nxF
 ) else (
   if exist "%%~dpnF.playable.mp4" (
     echo Existing output retained: %%~dpnF.playable.mp4
   ) else (
     "%FFMPEG%" -hide_banner -nostdin -v error -xerror -r !FPS! -i "%%~fF" -map 0:v:0 -an -c:v copy -movflags +faststart "%%~dpnF.pending.mp4"
     if not errorlevel 1 (
       "%FFMPEG%" -v error -i "%%~dpnF.pending.mp4" -frames:v 1 -f null NUL
       if not errorlevel 1 (move "%%~dpnF.pending.mp4" "%%~dpnF.playable.mp4" >nul & echo Saved %%~dpnF.playable.mp4)
     )
     if exist "%%~dpnF.pending.mp4" del /q "%%~dpnF.pending.mp4"
   )
 )
)
if !count! EQU 0 (echo No candidate HEVC files found & exit /b 5)
