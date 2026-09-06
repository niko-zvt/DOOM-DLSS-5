@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"
if not defined DOOMWADDIR set DOOMWADDIR=%~dp0wads
if not defined HOME set HOME=%USERPROFILE%

set MODE=original
set ARGS=

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--original" (
  set MODE=original
  shift
  goto parse
)
if /I "%~1"=="--dlss-on" (
  set MODE=dlss
  shift
  goto parse
)
if /I "%~1"=="--dlss" (
  set MODE=dlss
  shift
  goto parse
)
if /I "%~1"=="--dlss5" (
  set MODE=dlss5
  shift
  goto parse
)
if /I "%~1"=="--anime4k" (
  set MODE=anime4k
  shift
  goto parse
)
set ARGS=!ARGS! %1
shift
goto parse

:parsed
set EXE=%~dp0build-win\Release\windoom-%MODE%\windoom.exe
if not exist "%EXE%" (
  echo windoom.exe not found in windoom-%MODE%. Build first:
  echo   .\build.cmd
  echo   .\build.cmd --dlss-on
  echo   .\build.cmd --dlss5
  echo   .\build.cmd --anime4k
  exit /b 1
)

REM CWD stays Release so -playdemo compare.lmp and similar relative
REM files still resolve. DLLs load from the exe folder, not CWD.
set RUNDIR=%~dp0build-win\Release
if exist "%RUNDIR%" (
  cd /d "%RUNDIR%"
) else (
  cd /d "%~dp0"
)
"%EXE%"%ARGS%
