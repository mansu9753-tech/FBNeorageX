@echo off
setlocal enabledelayedexpansion
title FBNeoRageX Single EXE Build v1.9

set UCRT=C:\msys64\ucrt64
set QT_STATIC=%UCRT%\qt6-static
set CMAKE=%UCRT%\bin\cmake.exe
set NINJA=%UCRT%\bin\ninja.exe
set GXX=%UCRT%\bin\g++.exe
set GCC=%UCRT%\bin\gcc.exe
set SRC=C:\fbneoragex
set BLD=C:\fbneoragex\build_static

set PATH=%UCRT%\bin;%QT_STATIC%\bin;%PATH%

echo.
echo [1/4] Checking tools...
if not exist "%CMAKE%" ( echo ERROR: cmake not found & pause & exit /b 1 )
if not exist "%NINJA%"  ( echo ERROR: ninja not found  & pause & exit /b 1 )
if not exist "%GXX%"    ( echo ERROR: g++ not found    & pause & exit /b 1 )
if not exist "%QT_STATIC%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo ERROR: qt6-static not found at %QT_STATIC%
    echo Run in MSYS2: pacman -S mingw-w64-ucrt-x86_64-qt6-static
    pause & exit /b 1
)
echo   OK: cmake=%CMAKE%
echo   OK: Qt6 static=%QT_STATIC%

echo.
echo [2/4] Preparing build directory (clean)...
rem ── 실행 중일 수 있는 이전 EXE 종료 (ld 'Permission denied' 방지) ──
taskkill /F /IM FBNeoRageX.exe >NUL 2>&1
if exist "%BLD%" (
    echo   Removing old build cache...
    rmdir /s /q "%BLD%"
)
mkdir "%BLD%"

echo.
echo [3/4] CMake configure...

set CMAKEARGS=-S "%SRC%" -B "%BLD%"
set CMAKEARGS=%CMAKEARGS% -G Ninja
set CMAKEARGS=%CMAKEARGS% -DCMAKE_BUILD_TYPE=Release
set CMAKEARGS=%CMAKEARGS% -DCMAKE_PREFIX_PATH="%QT_STATIC%"
set CMAKEARGS=%CMAKEARGS% -DCMAKE_CXX_COMPILER="%GXX%"
set CMAKEARGS=%CMAKEARGS% -DCMAKE_C_COMPILER="%GCC%"
set CMAKEARGS=%CMAKEARGS% -DCMAKE_MAKE_PROGRAM="%NINJA%"
set CMAKEARGS=%CMAKEARGS% -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY
set CMAKEARGS=%CMAKEARGS% -DQT_FEATURE_openssl=OFF
set CMAKEARGS=%CMAKEARGS% -DQT_FEATURE_openssl_linked=OFF
rem Windows native TLS (Schannel) - HTTPS support without OpenSSL
rem Used by Qt6 Network for public IP lookup (netplay room code feature)
set CMAKEARGS=%CMAKEARGS% -DQT_FEATURE_schannel=ON

"%CMAKE%" %CMAKEARGS%

if %errorlevel% NEQ 0 ( echo ERROR: CMake configure failed & pause & exit /b 1 )

echo.
echo [4/4] Building...
"%CMAKE%" --build "%BLD%" --config Release -j4

if %errorlevel% NEQ 0 ( echo ERROR: Build failed & pause & exit /b 1 )

echo.
echo ===================================================
if exist "%BLD%\FBNeoRageX.exe" (
    for %%F in ("%BLD%\FBNeoRageX.exe") do set SZ=%%~zF
    set /a MB=!SZ! / 1048576
    echo BUILD SUCCESS  [v1.9]
    echo   EXE : %BLD%\FBNeoRageX.exe
    echo   Size: ~!MB! MB
    echo   TLS : Windows Schannel ^(HTTPS enabled^)
) else (
    echo BUILD FAILED -- EXE not found
)
echo ===================================================
echo.
pause
