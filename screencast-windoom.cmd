@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

echo Screencast queue: 10 playdemo runs, one window at a time.
echo Need .\build.cmd --all and compare.lmp in build-win\Release
echo.

call :run 1 10 "--original -playdemo compare -color"
if errorlevel 1 exit /b 1
call :run 2 10 "--ngx-dlss3.5 -playdemo compare -color"
if errorlevel 1 exit /b 1
call :run 3 10 "--ngx-dlss4 -playdemo compare -color"
if errorlevel 1 exit /b 1
call :run 4 10 "--ngx-dlss4.5 -playdemo compare -color"
if errorlevel 1 exit /b 1
call :run 5 10 "--ngx-dlss5 -playdemo compare -color"
if errorlevel 1 exit /b 1
call :run 6 10 "--anime4k -playdemo compare -color"
if errorlevel 1 exit /b 1
call :run 7 10 "--fsr2 -playdemo compare -color"
if errorlevel 1 exit /b 1
call :run 8 10 "--original -playdemo compare -depth"
if errorlevel 1 exit /b 1
call :run 9 10 "--original -playdemo compare -normal"
if errorlevel 1 exit /b 1
call :run 10 10 "--original -playdemo compare -velocity"
if errorlevel 1 exit /b 1

echo Done.
exit /b 0

:run
set STEP=%~1
set TOTAL=%~2
set FLAGS=%~3
echo [%STEP%/%TOTAL%] %FLAGS%
call "%~dp0play-windoom.cmd" %FLAGS%
if errorlevel 1 (
  echo Stopped at step %STEP%/%TOTAL%.
  exit /b 1
)
timeout /t 3 /nobreak >nul
exit /b 0
