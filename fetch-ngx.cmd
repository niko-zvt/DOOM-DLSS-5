@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\fetch-ngx.ps1" %*
exit /b %ERRORLEVEL%
