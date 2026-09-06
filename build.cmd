@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

set WANT_ORIGINAL=0
set WANT_NGX35=0
set WANT_NGX4=0
set WANT_NGX45=0
set WANT_NGX5=0
set WANT_ANIME4K=0
set WANT_FSR2=0
set CONFIG=Release
set ANY=0

:parse
if "%~1"=="" goto parsed
if /I "%~1"=="--original" set WANT_ORIGINAL=1& set ANY=1
if /I "%~1"=="--ngx-dlss3.5" set WANT_NGX35=1& set ANY=1
if /I "%~1"=="--ngx-dlss4" set WANT_NGX4=1& set ANY=1
if /I "%~1"=="--ngx-dlss4.5" set WANT_NGX45=1& set ANY=1
if /I "%~1"=="--ngx-dlss5" set WANT_NGX5=1& set ANY=1
if /I "%~1"=="--anime4k" set WANT_ANIME4K=1& set ANY=1
if /I "%~1"=="--fsr2" set WANT_FSR2=1& set ANY=1
if /I "%~1"=="--all" (
  set WANT_ORIGINAL=1
  set WANT_ANIME4K=1
  set WANT_FSR2=1
  set WANT_NGX35=1
  set WANT_NGX4=1
  set WANT_NGX45=1
  set WANT_NGX5=1
  set ANY=1
)
if /I "%~1"=="--help" goto usage
if /I "%~1"=="-h" goto usage
if /I "%~1"=="/?" goto usage
shift
goto parse

:usage
echo .\build.cmd                 nearest, no NVIDIA, no Anime4K
echo .\build.cmd --ngx-dlss3.5   Ray Reconstruction into windoom-ngx-dlss3.5
echo .\build.cmd --ngx-dlss4     Super Resolution preset K into windoom-ngx-dlss4
echo .\build.cmd --ngx-dlss4.5   Super Resolution preset L into windoom-ngx-dlss4.5
echo .\build.cmd --ngx-dlss5     same SR into windoom-ngx-dlss5 ^(Swapper^)
echo .\build.cmd --anime4k       Anime4K Fast into windoom-anime4k
echo .\build.cmd --fsr2          FSR 2.2 into windoom-fsr2
echo .\build.cmd --all           original + anime4k + fsr2 + all four NGX folders
echo.
echo Flags can be combined. NVIDIA / FSR2 sources stay out of git.
echo See win32\README-NGX.md, win32\README-ANIME4K.md, win32\README-FSR2.md
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
if "%WANT_NGX35%"=="1" set NEED_NGX=1
if "%WANT_NGX4%"=="1" set NEED_NGX=1
if "%WANT_NGX45%"=="1" set NEED_NGX=1
if "%WANT_NGX5%"=="1" set NEED_NGX=1

if "%NEED_NGX%"=="1" (
  echo == fetch NVIDIA DLSS SDK ==
  call "%~dp0fetch-ngx.cmd"
  if errorlevel 1 exit /b 1
)

if "%WANT_ORIGINAL%"=="1" (
  call :build_one OFF OFF OFF
  if errorlevel 1 exit /b 1
  set STAGE_DIR=windoom-original
  set STAGE_KIND=original
  call :stage_dir
  if errorlevel 1 exit /b 1
)

if "%WANT_ANIME4K%"=="1" (
  call :build_one OFF ON OFF
  if errorlevel 1 exit /b 1
  set STAGE_DIR=windoom-anime4k
  set STAGE_KIND=anime4k
  call :stage_dir
  if errorlevel 1 exit /b 1
)

if "%WANT_FSR2%"=="1" (
  echo == fetch AMD FSR2 sources ==
  call "%~dp0fetch-fsr2.cmd"
  if errorlevel 1 exit /b 1
  call :build_one OFF OFF ON
  if errorlevel 1 exit /b 1
  set STAGE_DIR=windoom-fsr2
  set STAGE_KIND=fsr2
  call :stage_dir
  if errorlevel 1 exit /b 1
)

if "%NEED_NGX%"=="1" (
  call :build_one ON OFF OFF
  if errorlevel 1 exit /b 1
  if "%WANT_NGX35%"=="1" (
    set STAGE_DIR=windoom-ngx-dlss3.5
    set STAGE_KIND=ngx35
    call :stage_dir
    if errorlevel 1 exit /b 1
  )
  if "%WANT_NGX4%"=="1" (
    set STAGE_DIR=windoom-ngx-dlss4
    set STAGE_KIND=ngx4
    call :stage_dir
    if errorlevel 1 exit /b 1
  )
  if "%WANT_NGX45%"=="1" (
    set STAGE_DIR=windoom-ngx-dlss4.5
    set STAGE_KIND=ngx45
    call :stage_dir
    if errorlevel 1 exit /b 1
  )
  if "%WANT_NGX5%"=="1" (
    set STAGE_DIR=windoom-ngx-dlss5
    set STAGE_KIND=ngx5
    call :stage_dir
    if errorlevel 1 exit /b 1
  )
  call "%~dp0fetch-ngx.cmd"
  if errorlevel 1 exit /b 1
)

echo.
echo Staged folders under build-win\%CONFIG%\
if "%WANT_ORIGINAL%"=="1" echo   windoom-original
if "%WANT_NGX35%"=="1" echo   windoom-ngx-dlss3.5
if "%WANT_NGX4%"=="1" echo   windoom-ngx-dlss4
if "%WANT_NGX45%"=="1" echo   windoom-ngx-dlss4.5
if "%WANT_NGX5%"=="1" echo   windoom-ngx-dlss5
if "%WANT_ANIME4K%"=="1" echo   windoom-anime4k
if "%WANT_FSR2%"=="1" echo   windoom-fsr2
echo Run play-windoom.cmd [--ngx-dlss3.5^|--ngx-dlss4^|--ngx-dlss4.5^|--ngx-dlss5^|--anime4k^|--fsr2]
exit /b 0

:build_one
set NGXFLAG=%~1
set A4KFLAG=%~2
set FSR2FLAG=%~3
echo == configure WINDOOM_NGX=%NGXFLAG% WINDOOM_ANIME4K=%A4KFLAG% WINDOOM_FSR2=%FSR2FLAG% ==
if exist "%~dp0build-win\CMakeCache.txt" (
  "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -DWINDOOM_NGX=%NGXFLAG% -DWINDOOM_ANIME4K=%A4KFLAG% -DWINDOOM_FSR2=%FSR2FLAG%
) else (
  "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -G "Visual Studio 18 2026" -A x64 -DWINDOOM_NGX=%NGXFLAG% -DWINDOOM_ANIME4K=%A4KFLAG% -DWINDOOM_FSR2=%FSR2FLAG%
  if errorlevel 1 "%CMAKE%" -S "%~dp0." -B "%~dp0build-win" -G "Visual Studio 17 2022" -A x64 -DWINDOOM_NGX=%NGXFLAG% -DWINDOOM_ANIME4K=%A4KFLAG% -DWINDOOM_FSR2=%FSR2FLAG%
)
if errorlevel 1 exit /b 1
echo == build %CONFIG% ==
"%CMAKE%" --build "%~dp0build-win" --config %CONFIG%
if errorlevel 1 exit /b 1
exit /b 0

:stage_dir
set DEST=%~dp0build-win\%CONFIG%\%STAGE_DIR%
set KIND=%STAGE_KIND%
set SRC=%~dp0build-win\%CONFIG%\windoom.exe
if not exist "%SRC%" (
  echo windoom.exe missing after build
  exit /b 1
)
mkdir "%DEST%" 2>nul
copy /Y "%SRC%" "%DEST%\windoom.exe" >nul
if /I "%KIND%"=="ngx35" (
  >"%DEST%\ngx.mode" echo rr
  >"%DEST%\README.txt" echo DLSS 3.5 Ray Reconstruction. Albedo/roughness are synthesized. See win32\README-NGX.md
)
if /I "%KIND%"=="ngx4" (
  >"%DEST%\ngx.mode" echo k
  >"%DEST%\README.txt" echo DLSS 4 Super Resolution, preset K. See win32\README-NGX.md
)
if /I "%KIND%"=="ngx45" (
  >"%DEST%\ngx.mode" echo l
  >"%DEST%\README.txt" echo DLSS 4.5 Super Resolution, preset L. See win32\README-NGX.md
)
if /I "%KIND%"=="ngx5" (
  >"%DEST%\ngx.mode" echo dlss5
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
if /I "%KIND%"=="fsr2" (
  >"%DEST%\README.txt" echo FSR 2.2 320 to 1280. See win32\README-FSR2.md
)
echo Staged %DEST%
exit /b 0
