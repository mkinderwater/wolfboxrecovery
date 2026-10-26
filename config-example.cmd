@echo off
rem Configure these before pressing Recover Orphan MP4 in the GUI.
rem Source image is chosen in the main GUI for conventional recovery.
set "WOLFBOX_ORPHAN_OUTPUT=Orphan_Output"
set "WOLFBOX_FPS=25"
rem Set paths below only if ffmpeg / ffprobe are not on PATH.
rem set "WOLFBOX_FFMPEG=C:\Tools\ffmpeg.exe"
rem set "WOLFBOX_FFPROBE=C:\Tools\ffprobe.exe"
start "" WolfBoxRecovery.exe
