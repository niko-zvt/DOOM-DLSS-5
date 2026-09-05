@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set DLSS=0
set CONFIG=Release

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--dlss-on" set DLSS=1
if /I "%~1"=="--dlss" set DLSS=1
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="/?" goto usage
shift
goto parse

:usage
echo .\build.cmd            GPLv2 build, nearest present, no NVIDIA SDK
echo .\build.cmd --dlss-on  fetch NVIDIA/DLSS into third_party\ngx and link NGX
echo.
echo NVIDIA files stay out of git. See win32\README-NGX.md
exit /b 0

:parsed
set CMAKE=
where cmake >nul 2>&1 && set "CMAKE=cmake"
if not defined CMAKE if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
  set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if not defined CMAKE if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
  set "CMAKE=%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
if not defined CMAKE (
  echo cmake not found. Install Visual Studio C++ / CMake tools.
  exit /b 1
)

if "%DLSS%"=="1" (
  echo == fetch NVIDIA DLSS SDK ==
  call "%~dp0fetch-ngx.cmd"
  if errorlevel 1 exit /b 1
  set NGXFLAG=ON
) else (
  set NGXFLAG=OFF
)

echo == configure WINDOOM_NGX=%NGXFLAG% ==
if exist "%~dp0build-win\CMakeCache.txt" (
  "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -DWINDOOM_NGX=%NGXFLAG%
) else (
  "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -G "Visual Studio 18 2026" -A x64 -DWINDOOM_NGX=%NGXFLAG%
  if errorlevel 1 "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -G "Visual Studio 17 2022" -A x64 -DWINDOOM_NGX=%NGXFLAG%
)
if errorlevel 1 exit /b 1

echo == build %CONFIG% ==
"%CMAKE%" --build "%~dp0build-win" --config %CONFIG%
if errorlevel 1 exit /b 1

if "%DLSS%"=="1" (
  echo == copy nvngx_dlss.dll beside exe ==
  call "%~dp0fetch-ngx.cmd"
  if errorlevel 1 exit /b 1
)

echo.
if "%DLSS%"=="1" (
  echo Build with NGX. Run play-windoom.cmd
) else (
  echo Build without NGX. Run play-windoom.cmd
)
exit /b 0
