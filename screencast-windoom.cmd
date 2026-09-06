@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM Captured up front: inside "call :run" %0 is the label, not this file.
set ROOT=%~dp0
set EXPORT=

REM Parse before cd so a relative --export path resolves against the caller's cwd.
:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--export" (
  if "%~2"=="" (
    echo --export needs a path.
    goto usage
  )
  for %%I in ("%~2") do set EXPORT=%%~fI
  if "!EXPORT:~-1!"=="\" set EXPORT=!EXPORT:~0,-1!
  shift
  shift
  goto parse
)
echo Unknown argument: %~1
:usage
echo Usage: screencast-windoom.cmd [--export ^<path^>]
echo   --export ^<path^>  write every frame of every run as PNG into
echo                    ^<path^>\windoom-^<mode^>[-^<view^>]\f000001.png ...
exit /b 1

:parsed
cd /d "%ROOT%"
echo Screencast queue: 10 playdemo runs, one window at a time.
echo Need .\build.cmd --all and compare.lmp in build-win\Release
if defined EXPORT (
  if not exist "%EXPORT%" mkdir "%EXPORT%"
  if not exist "%EXPORT%" (
    echo Cannot create %EXPORT%
    exit /b 1
  )
  echo Exporting PNG frames to %EXPORT%
)
echo.

call :run 1 10 "--original -playdemo compare -color" windoom-original
if errorlevel 1 exit /b 1
call :run 2 10 "--ngx-dlss3.5 -playdemo compare -color" windoom-ngx-dlss3.5
if errorlevel 1 exit /b 1
call :run 3 10 "--ngx-dlss4 -playdemo compare -color" windoom-ngx-dlss4
if errorlevel 1 exit /b 1
call :run 4 10 "--ngx-dlss4.5 -playdemo compare -color" windoom-ngx-dlss4.5
if errorlevel 1 exit /b 1
call :run 5 10 "--ngx-dlss5 -playdemo compare -color" windoom-ngx-dlss5
if errorlevel 1 exit /b 1
call :run 6 10 "--anime4k -playdemo compare -color" windoom-anime4k
if errorlevel 1 exit /b 1
call :run 7 10 "--fsr2 -playdemo compare -color" windoom-fsr2
if errorlevel 1 exit /b 1
call :run 8 10 "--original -playdemo compare -depth" windoom-original-depth
if errorlevel 1 exit /b 1
call :run 9 10 "--original -playdemo compare -normal" windoom-original-normal
if errorlevel 1 exit /b 1
call :run 10 10 "--original -playdemo compare -velocity" windoom-original-velocity
if errorlevel 1 exit /b 1

echo Done.
if defined EXPORT (
  echo Assemble a run with ffmpeg, e.g.:
  echo   ffmpeg -framerate 35 -i "%EXPORT%\windoom-ngx-dlss4\f%%06d.png" -c:v libx264 -pix_fmt yuv420p dlss4.mp4
)
exit /b 0

:run
set STEP=%~1
set TOTAL=%~2
set FLAGS=%~3
set SUB=%~4
if defined EXPORT set FLAGS=%FLAGS% -export "%EXPORT%\%SUB%"
echo [%STEP%/%TOTAL%] %FLAGS%
call "%ROOT%play-windoom.cmd" %FLAGS%
if errorlevel 1 (
  echo Stopped at step %STEP%/%TOTAL%.
  exit /b 1
)
timeout /t 3 /nobreak >nul
exit /b 0
