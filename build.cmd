@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set WANT_ORIGINAL=0
set WANT_DLSS=0
set WANT_DLSS5=0
set WANT_ANIME4K=0
set CONFIG=Release
set ANY=0

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--original" set WANT_ORIGINAL=1& set ANY=1
if /I "%~1"=="--dlss-on" set WANT_DLSS=1& set ANY=1
if /I "%~1"=="--dlss" set WANT_DLSS=1& set ANY=1
if /I "%~1"=="--dlss5" set WANT_DLSS5=1& set ANY=1
if /I "%~1"=="--anime4k" set WANT_ANIME4K=1& set ANY=1
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="/?" goto usage
shift
goto parse

:usage
echo .\build.cmd              nearest, no NVIDIA, no Anime4K
echo .\build.cmd --dlss-on    NGX upscale into windoom-dlss
echo .\build.cmd --dlss5      same NGX exe into windoom-dlss5
echo .\build.cmd --anime4k    Anime4K Fast into windoom-anime4k
echo.
echo Flags can be combined. NVIDIA files stay out of git.
echo See win32\README-NGX.md and win32\README-ANIME4K.md
exit /b 0

:parsed
if "%ANY%"=="0" set WANT_ORIGINAL=1

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

set NEED_NGX=0
if "%WANT_DLSS%"=="1" set NEED_NGX=1
if "%WANT_DLSS5%"=="1" set NEED_NGX=1

if "%NEED_NGX%"=="1" (
  echo == fetch NVIDIA DLSS SDK ==
  call "%~dp0fetch-ngx.cmd"
  if errorlevel 1 exit /b 1
)

if "%WANT_ORIGINAL%"=="1" (
  call :build_one OFF OFF
  if errorlevel 1 exit /b 1
  call :stage_dir windoom-original original
  if errorlevel 1 exit /b 1
)

if "%WANT_ANIME4K%"=="1" (
  call :build_one OFF ON
  if errorlevel 1 exit /b 1
  call :stage_dir windoom-anime4k anime4k
  if errorlevel 1 exit /b 1
)

if "%NEED_NGX%"=="1" (
  call :build_one ON OFF
  if errorlevel 1 exit /b 1
  if "%WANT_DLSS%"=="1" (
    call :stage_dir windoom-dlss dlss
    if errorlevel 1 exit /b 1
  )
  if "%WANT_DLSS5%"=="1" (
    call :stage_dir windoom-dlss5 dlss5
    if errorlevel 1 exit /b 1
  )
  call "%~dp0fetch-ngx.cmd"
  if errorlevel 1 exit /b 1
)

echo.
echo Staged folders under build-win\%CONFIG%\
if "%WANT_ORIGINAL%"=="1" echo   windoom-original
if "%WANT_DLSS%"=="1" echo   windoom-dlss
if "%WANT_DLSS5%"=="1" echo   windoom-dlss5
if "%WANT_ANIME4K%"=="1" echo   windoom-anime4k
echo Run play-windoom.cmd [--dlss-on^|--dlss5^|--anime4k]
exit /b 0

:build_one
set NGXFLAG=%~1
set A4KFLAG=%~2
echo == configure WINDOOM_NGX=%NGXFLAG% WINDOOM_ANIME4K=%A4KFLAG% ==
if exist "%~dp0build-win\CMakeCache.txt" (
  "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -DWINDOOM_NGX=%NGXFLAG% -DWINDOOM_ANIME4K=%A4KFLAG%
) else (
  "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -G "Visual Studio 18 2026" -A x64 -DWINDOOM_NGX=%NGXFLAG% -DWINDOOM_ANIME4K=%A4KFLAG%
  if errorlevel 1 "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -G "Visual Studio 17 2022" -A x64 -DWINDOOM_NGX=%NGXFLAG% -DWINDOOM_ANIME4K=%A4KFLAG%
)
if errorlevel 1 exit /b 1
echo == build %CONFIG% ==
"%CMAKE%" --build "%~dp0build-win" --config %CONFIG%
if errorlevel 1 exit /b 1
exit /b 0

:stage_dir
set DEST=%~dp0build-win\%CONFIG%\%~1
set KIND=%~2
set SRC=%~dp0build-win\%CONFIG%\windoom.exe
if not exist "%SRC%" (
  echo windoom.exe missing after build
  exit /b 1
)
mkdir "%DEST%" 2>nul
copy /Y "%SRC%" "%DEST%\windoom.exe" >nul
if /I "%KIND%"=="dlss5" (
  >"%DEST%\README.txt" echo Add this folder in DLSS5-Swapper. DirectX 12, Native.
  >>"%DEST%\README.txt" echo Do not drop your own dxgi.dll here first.
)
if /I "%KIND%"=="anime4k" (
  mkdir "%DEST%\shaders\anime4k" 2>nul
  copy /Y "%~dp0win32\shaders\anime4k\*.hlsl" "%DEST%\shaders\anime4k\" >nul
  copy /Y "%~dp0win32\shaders\anime4k\*.hlsli" "%DEST%\shaders\anime4k\" >nul 2>nul
  >"%DEST%\README.txt" echo Anime4K Fast Mode C present. See win32\README-ANIME4K.md
)
if /I "%KIND%"=="original" (
  >"%DEST%\README.txt" echo Nearest 4x present. No NVIDIA, no Anime4K.
)
if /I "%KIND%"=="dlss" (
  >"%DEST%\README.txt" echo Classic DLSS 320 to 1280. See win32\README-NGX.md
)
echo Staged %DEST%
exit /b 0
