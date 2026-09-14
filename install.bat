@echo off
REM ==========================================================================
REM  Ghost Engine MPV Setup
REM ==========================================================================
REM  Double-click this. It installs mpv, yt-dlp and the Ghost Engine config
REM  into C:\mpv (or anywhere else you point it at).
REM
REM  Nothing here needs administrator rights, and no .exe is shipped with this
REM  download - mpv and yt-dlp come from the projects that publish them, and
REM  yt-dlp is checked against its published SHA-256 before it is installed.
REM
REM  Undo it with uninstall.bat.
REM ==========================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0lib\install.ps1" %*
if errorlevel 1 (
    echo.
    echo   Setup did not finish. The reason is printed above.
    pause
)
