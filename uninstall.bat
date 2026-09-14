@echo off
REM Removes the folder install.bat created, and nothing else.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0lib\uninstall.ps1" %*
