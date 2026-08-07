:: Fetch this plugin's Windows build dependencies: a prebuilt wxWidgets
:: for MSVC, and OpenCPN's own import library (opencpn.lib).
::
:: Adapted from testplugin_pi/Frontend2's own msvc/win_deps.bat (a real,
:: verified, working script from OpenCPN's standard plugin build
:: template) -- simplified for this project specifically, since we
:: don't (yet) use that template's own CMake module system, and don't
:: need its Cloudsmith/NSIS/localization tooling just to produce a
:: local build. The wxWidgets download URLs and 7z extraction steps
:: below are the same ones that template actually uses.
::
:: Requires: git, cmake, and 7-Zip (7z on PATH) already installed --
:: see README.md's "Building on Windows" section for getting those.
::
:: Usage:
::   cd msvc
::   win_deps.bat
::
:: Output:
::   ..\cache\wxWidgets-3.2.1\        -- extracted wxWidgets headers + libs
::   ..\cache\opencpn.lib             -- OpenCPN's import library
::   ..\cache\wx-config.bat           -- sets wxWidgets_ROOT_DIR/LIB_DIR
::                                        for the actual cmake invocation
::
:: After running this, build from the project root with:
::   mkdir build && cd build
::   call ..\cache\wx-config.bat
::   cmake -A Win32 -G "Visual Studio 17 2022" ^
::       -DwxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR% ^
::       -DwxWidgets_LIB_DIR=%wxWidgets_LIB_DIR% ^
::       -DOPENCPN_IMPORT_LIB=..\cache\opencpn.lib ^
::       ..
::   cmake --build . --config Release
::
:: Honest status: this script has NOT been run on an actual Windows
:: machine as part of this project -- see README.md's "Building on
:: Windows" section for the same caveat that applies to the whole
:: Windows build path. It's adapted directly from a script that is
:: independently known to work (the real testplugin_pi template, used
:: by many published OpenCPN plugins), rather than written from
:: scratch, but "adapted from working code" and "verified working here"
:: are different things. Please report any issues this surfaces.

@echo off
setlocal enabledelayedexpansion

set "SCRIPTDIR=%~dp0"
if not exist "%SCRIPTDIR%..\cache" mkdir "%SCRIPTDIR%..\cache"
set "CONFIG_FILE=%SCRIPTDIR%..\cache\wx-config.bat"

set "WX_VERSION=3.2.1"
set "WXWIN=%SCRIPTDIR%..\cache\wxWidgets-%WX_VERSION%"
set "wxWidgets_ROOT_DIR=%WXWIN%"
set "wxWidgets_LIB_DIR=%WXWIN%\lib\vc14x_dll"

git --version >nul 2>&1
if errorlevel 1 (
  echo git not found on PATH -- install it first ^(e.g. via choco install git^).
  exit /b 1
)

cmake --version >nul 2>&1
if errorlevel 1 (
  echo cmake not found on PATH -- install it first ^(e.g. via choco install cmake^).
  exit /b 1
)

7z i >nul 2>&1
if errorlevel 1 (
  echo 7z not found on PATH -- install 7-Zip first ^(e.g. via choco install 7zip^).
  exit /b 1
)

wget --version >nul 2>&1
if errorlevel 1 (
  echo wget not found on PATH -- install it first ^(e.g. via choco install wget^).
  exit /b 1
)

:: Write the config file callers source to pick up wxWidgets_ROOT_DIR/
:: LIB_DIR, regardless of whether the download below actually ran this
:: time (e.g. on a repeat run where wxWidgets is already cached).
echo set "wxWidgets_ROOT_DIR=%wxWidgets_ROOT_DIR%" > "%CONFIG_FILE%"
echo set "wxWidgets_LIB_DIR=%wxWidgets_LIB_DIR%" >> "%CONFIG_FILE%"

if not exist "%WXWIN%" (
  echo Downloading wxWidgets %WX_VERSION% for MSVC...
  set "GITHUB_DL=https://github.com/wxWidgets/wxWidgets/releases/download"
  mkdir "%WXWIN%"
  wget -nv --no-check-certificate !GITHUB_DL!/v%WX_VERSION%/wxMSW-%WX_VERSION%_vc14x_Dev.7z
  if errorlevel 1 (
    echo Failed to download wxMSW-%WX_VERSION%_vc14x_Dev.7z -- check your network connection or whether this release asset still exists at that URL.
    exit /b 1
  )
  7z x -o"%WXWIN%" wxMSW-%WX_VERSION%_vc14x_Dev.7z
  wget -nv --no-check-certificate !GITHUB_DL!/v%WX_VERSION%/wxWidgets-%WX_VERSION%-headers.7z
  if errorlevel 1 (
    echo Failed to download wxWidgets-%WX_VERSION%-headers.7z -- check your network connection or whether this release asset still exists at that URL.
    exit /b 1
  )
  7z x -o"%WXWIN%" wxWidgets-%WX_VERSION%-headers.7z
) else (
  echo wxWidgets %WX_VERSION% already present at %WXWIN%, skipping download.
)

if not exist "%SCRIPTDIR%..\cache\opencpn.lib" (
  echo Downloading opencpn.lib ^(OpenCPN's own Windows import library^)...
  pushd "%SCRIPTDIR%..\cache"
  wget -nv https://sourceforge.net/projects/opencpnplugins/files/opencpn.lib
  popd
  if not exist "%SCRIPTDIR%..\cache\opencpn.lib" (
    echo Failed to download opencpn.lib -- check your network connection or whether this file still exists at that URL. See README.md for the fallback of extracting it from your own OpenCPN Windows installation instead.
    exit /b 1
  )
) else (
  echo opencpn.lib already present in cache, skipping download.
)

:: libs/api-18/CMakeLists.txt (the vendored OpenCPN API headers,
:: unrelated to this project's own -DOPENCPN_IMPORT_LIB mechanism used
:: above) has its own, separate, hardcoded expectation for MSVC builds:
:: it links against libs/api-18/msvc-wx32/opencpn.lib directly,
:: regardless of what's passed via -DOPENCPN_IMPORT_LIB. Both need the
:: file to actually link successfully, so it's copied to both
:: locations here rather than only fetched once.
if not exist "%SCRIPTDIR%..\libs\api-18\msvc-wx32" mkdir "%SCRIPTDIR%..\libs\api-18\msvc-wx32"
copy /y "%SCRIPTDIR%..\cache\opencpn.lib" "%SCRIPTDIR%..\libs\api-18\msvc-wx32\opencpn.lib" >nul

echo.
echo Done. wxWidgets and opencpn.lib are in %SCRIPTDIR%..\cache\
echo (opencpn.lib is also copied to libs\api-18\msvc-wx32\, which the
echo vendored API headers there separately expect it at.)
echo Run "call ..\cache\wx-config.bat" before invoking cmake to pick up
echo wxWidgets_ROOT_DIR and wxWidgets_LIB_DIR.
