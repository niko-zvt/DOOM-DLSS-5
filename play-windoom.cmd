@echo off
setlocal
cd /d "%~dp0"
if not defined DOOMWADDIR set DOOMWADDIR=%~dp0wads
if not defined HOME set HOME=%USERPROFILE%

set EXE=%~dp0build-win\Release\windoom.exe
if not exist "%EXE%" set EXE=%~dp0build-win\windoom.exe
if not exist "%EXE%" (
  echo windoom.exe not found. Build first:
  echo   .\build.cmd
  echo   .\build.cmd --dlss-on
  exit /b 1
)

for %%I in ("%EXE%") do set EXEDIR=%%~dpI
cd /d "%EXEDIR%"
"%EXE%" %*
