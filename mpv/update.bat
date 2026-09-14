@echo off
REM Updates yt-dlp - the piece that breaks when YouTube changes its player.
REM Fetched from yt-dlp's own release and checked against its published SHA-256.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update.ps1" %*
