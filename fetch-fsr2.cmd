@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\fetch-fsr2.ps1" %*
exit /b %ERRORLEVEL%
