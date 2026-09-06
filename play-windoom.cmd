@echo off
setlocal EnableExtensions EnableDelayedExpansion
REM Capture before the parse loop: "shift" also shifts %0, so %~dp0 would
REM later resolve against the last argument (e.g. an -export path).
set ROOT=%~dp0
cd /d "%ROOT%"
if not defined DOOMWADDIR set DOOMWADDIR=%ROOT%wads
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
if /I "%~1"=="--ngx-dlss3.5" (
  set MODE=ngx-dlss3.5
  shift
  goto parse
)
if /I "%~1"=="--ngx-dlss4" (
  set MODE=ngx-dlss4
  shift
  goto parse
)
if /I "%~1"=="--ngx-dlss4.5" (
  set MODE=ngx-dlss4.5
  shift
  goto parse
)
if /I "%~1"=="--ngx-dlss5" (
  set MODE=ngx-dlss5
  shift
  goto parse
)
if /I "%~1"=="--anime4k" (
  set MODE=anime4k
  shift
  goto parse
)
if /I "%~1"=="--fsr2" (
  set MODE=fsr2
  shift
  goto parse
)
set ARGS=!ARGS! %1
shift
goto parse

:parsed
set EXE=%ROOT%build-win\Release\windoom-%MODE%\windoom.exe
if not exist "%EXE%" (
  echo windoom.exe not found in windoom-%MODE%. Build first:
  echo   .\build.cmd
  echo   .\build.cmd --ngx-dlss3.5
  echo   .\build.cmd --ngx-dlss4
  echo   .\build.cmd --ngx-dlss4.5
  echo   .\build.cmd --ngx-dlss5
  echo   .\build.cmd --anime4k
  echo   .\build.cmd --fsr2
  echo   .\build.cmd --all
  exit /b 1
)

REM CWD stays Release so -playdemo compare.lmp and similar relative
REM files still resolve. DLLs load from the exe folder, not CWD.
set RUNDIR=%ROOT%build-win\Release
if exist "%RUNDIR%" (
  cd /d "%RUNDIR%"
) else (
  cd /d "%ROOT%"
)
"%EXE%"%ARGS%
