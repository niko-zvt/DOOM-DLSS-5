@echo off
setlocal
cd /d "%~dp0"
if not defined DOOMWADDIR set DOOMWADDIR=%~dp0wads
if not defined HOME set HOME=%USERPROFILE%

set EXE=%~dp0build-win\Release\windoom.exe
if not exist "%EXE%" set EXE=%~dp0build-win\windoom.exe
if not exist "%EXE%" (
  echo windoom.exe not found. Build first:
  echo   cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64
  echo   cmake --build build-win --config Release
  exit /b 1
)

"%EXE%" %*
