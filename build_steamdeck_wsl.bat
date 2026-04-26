@echo off
setlocal enabledelayedexpansion
title FBNeoRageX - Steam Deck AppImage Builder (WSL2)

set SRC=C:\fbneoragex
set OUTPUT=%SRC%\build_linux\FBNeoRageX-linux-x86_64.tar.gz

echo.
echo ===================================================
echo   FBNeoRageX - Steam Deck AppImage Builder
echo ===================================================
echo.

:: [1] Check wsl.exe
echo [1/4] Checking WSL...
where wsl.exe >nul 2>&1
if %errorlevel% NEQ 0 goto :NO_WSL
echo   OK: wsl.exe found
goto :FIND_DISTRO

:NO_WSL
echo.
echo  ERROR: wsl.exe not found.
echo  Open PowerShell as Admin and run: wsl --install
echo  Then reboot, finish Ubuntu setup, and run this again.
echo.
pause
exit /b 1

:: [2] Find Ubuntu distro
:FIND_DISTRO
echo [2/4] Finding Ubuntu distro...
set DISTRO=
for %%d in (Ubuntu ubuntu Ubuntu-22.04 Ubuntu-24.04) do (
    if "!DISTRO!"=="" (
        wsl -d %%d --exec echo ok >nul 2>&1
        if !errorlevel! EQU 0 set DISTRO=%%d
    )
)

if "!DISTRO!"=="" goto :NO_DISTRO
echo   OK: distro = !DISTRO!
goto :CONVERT_PATH

:NO_DISTRO
echo.
echo  ERROR: No Ubuntu WSL2 distro found.
echo  Run in PowerShell: wsl --install -d Ubuntu
echo  Or install Ubuntu 22.04 from Microsoft Store.
echo.
wsl --list 2>&1
echo.
pause
exit /b 1

:: [3] Convert path
:CONVERT_PATH
echo [3/4] Converting path...
for /f "delims=" %%i in ('wsl -d !DISTRO! --exec wslpath -u "%SRC%"') do set WSL_SRC=%%i

if "!WSL_SRC!"=="" goto :NO_PATH
echo   OK: %SRC%
echo       -^> !WSL_SRC!
goto :BUILD

:NO_PATH
echo  ERROR: Path conversion failed.
pause
exit /b 1

:: [4] Build
:BUILD
echo.
echo [4/4] Starting build...
echo   First run : 10-20 min  (Qt6 install included)
echo   Rebuild   :  3-5  min
echo.

wsl -d !DISTRO! --exec sed -i "s/\r//" "!WSL_SRC!/build_steamdeck_wsl_helper.sh"
wsl -d !DISTRO! --exec sed -i "s/\r//" "!WSL_SRC!/build_steamdeck.sh"
wsl -d !DISTRO! --exec chmod +x "!WSL_SRC!/build_steamdeck_wsl_helper.sh" "!WSL_SRC!/build_steamdeck.sh"
wsl -d !DISTRO! --exec bash "!WSL_SRC!/build_steamdeck_wsl_helper.sh" "!WSL_SRC!"
set BUILD_RC=%errorlevel%

echo.
if %BUILD_RC% NEQ 0 goto :BUILD_FAIL

:: Result
echo ===================================================
if exist "%OUTPUT%" goto :SUCCESS

echo   WARN: AppImage not found at Windows path.
echo   Check WSL: ~/FBNeoRageX_wsl_build/build_linux/
echo ===================================================
pause
exit /b 0

:SUCCESS
for %%F in ("%OUTPUT%") do set SZ=%%~zF
set /a MB=%SZ% / 1048576
echo   BUILD SUCCESS
echo   Package  : %OUTPUT%
echo   Size     : ~%MB% MB
echo.
echo   On Steam Deck:
echo     chmod +x FBNeoRageX-x86_64.AppImage
echo     ./FBNeoRageX-x86_64.AppImage
echo ===================================================
echo.
explorer "%SRC%\build_linux"
pause
exit /b 0

:BUILD_FAIL
echo  ERROR: Build failed (exit code: %BUILD_RC%)
echo  Log: %SRC%\build_linux.log
pause
exit /b 1
