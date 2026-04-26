@echo off
setlocal EnableDelayedExpansion

echo ============================================
echo  FBNeoRageX C++ - Windows Build
echo ============================================
echo.

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%SCRIPT_DIR%\build_win"
set "IS_MSYS2=0"
set "USE_NINJA=0"
set "QT_DIR="

if defined QTDIR (
    if exist "%QTDIR%\lib\cmake\Qt6\Qt6Config.cmake" (
        set "QT_DIR=%QTDIR%"
        goto :qt_found
    )
)

if exist "C:\msys64\ucrt64\lib\cmake\Qt6\Qt6Config.cmake" (
    set "QT_DIR=C:\msys64\ucrt64"
    goto :qt_found
)
if exist "C:\msys64\mingw64\lib\cmake\Qt6\Qt6Config.cmake" (
    set "QT_DIR=C:\msys64\mingw64"
    goto :qt_found
)

for %%V in (6.9.0 6.8.3 6.8.0 6.7.3 6.7.2 6.7.0 6.6.3 6.5.3) do (
    for %%C in (msvc2022_64 msvc2019_64 mingw_64) do (
        if exist "C:\Qt\%%V\%%C\lib\cmake\Qt6\Qt6Config.cmake" (
            set "QT_DIR=C:\Qt\%%V\%%C"
            goto :qt_found
        )
    )
)

echo [ERROR] Qt6 not found.
echo.
echo Install: pacman -S mingw-w64-ucrt-x86_64-qt6-base
echo Or set:  set QTDIR=C:\Qt\6.x.x\msvc2019_64
echo.
pause
exit /b 1

:qt_found
echo [OK] Qt6: %QT_DIR%

echo %QT_DIR% | findstr /i "msys64" > nul 2>&1
if not errorlevel 1 (
    set "IS_MSYS2=1"
    set "PATH=%QT_DIR%\bin;C:\msys64\usr\bin;%PATH%"
    echo [INFO] MSYS2 detected
)

cmake --version > nul 2>&1
if errorlevel 1 (
    echo [ERROR] CMake not found.
    echo         pacman -S mingw-w64-ucrt-x86_64-cmake
    pause
    exit /b 1
)
echo [OK] CMake found

ninja --version > nul 2>&1
if not errorlevel 1 (
    set "USE_NINJA=1"
    echo [OK] Ninja found
) else (
    echo [INFO] Ninja not found - using MinGW Makefiles
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
if exist "%BUILD_DIR%\CMakeCache.txt" (
    del /q "%BUILD_DIR%\CMakeCache.txt"
    echo [INFO] Cleared CMakeCache.txt
)

echo.
echo [1/3] CMake configure...

if "%USE_NINJA%"=="1" (
    cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_BUILD_TYPE=Release -G "Ninja"
) else (
    cmake -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" -DCMAKE_PREFIX_PATH="%QT_DIR%" -DCMAKE_BUILD_TYPE=Release -G "MinGW Makefiles"
)

if errorlevel 1 (
    echo [ERROR] CMake configure failed
    pause
    exit /b 1
)

echo.
echo [2/3] Building...
cmake --build "%BUILD_DIR%" --config Release --parallel

if errorlevel 1 (
    echo [ERROR] Build failed
    pause
    exit /b 1
)

set "EXE_DIR=%BUILD_DIR%"
if exist "%BUILD_DIR%\Release\FBNeoRageX.exe" set "EXE_DIR=%BUILD_DIR%\Release"

echo.
echo [3/3] Deploying DLLs...

set "WDQT="
if exist "%QT_DIR%\bin\windeployqt6.exe" set "WDQT=%QT_DIR%\bin\windeployqt6.exe"
if not defined WDQT (
    if exist "%QT_DIR%\bin\windeployqt.exe" set "WDQT=%QT_DIR%\bin\windeployqt.exe"
)

if defined WDQT (
    echo [OK] Running: %WDQT%
    "%WDQT%" "%EXE_DIR%\FBNeoRageX.exe" --no-translations
    echo [OK] Qt DLLs deployed
) else (
    echo [WARN] windeployqt not found
)

if "%IS_MSYS2%"=="1" (
    echo [INFO] Copying MSYS2 runtime DLLs...
    set "MB=%QT_DIR%\bin"
    call :cdll libgcc_s_seh-1.dll
    call :cdll libstdc++-6.dll
    call :cdll libwinpthread-1.dll
    call :cdll libgomp-1.dll
    call :cdll libdouble-conversion.dll
    call :cdll libb2-1.dll
    call :cdll libbrotlidec.dll
    call :cdll libbrotlicommon.dll
    call :cdll libbz2-1.dll
    call :cdll libfreetype-6.dll
    call :cdll libglib-2.0-0.dll
    call :cdll libgraphite2.dll
    call :cdll libharfbuzz-0.dll
    call :cdll libintl-8.dll
    call :cdll libiconv-2.dll
    call :cdll libmd4c.dll
    call :cdll libpcre2-8-0.dll
    call :cdll libpcre2-16-0.dll
    call :cdll libpng16-16.dll
    call :cdll libzstd.dll
    call :cdll zlib1.dll
    call :cdll libicudt78.dll
    call :cdll libicuin78.dll
    call :cdll libicuuc78.dll
    call :cdll libicudt77.dll
    call :cdll libicuin77.dll
    call :cdll libicuuc77.dll
    call :cdll libicudt76.dll
    call :cdll libicuin76.dll
    call :cdll libicuuc76.dll
    echo [OK] MSYS2 runtime DLLs done
)

if exist "%SCRIPT_DIR%fbneo_libretro.dll" (
    copy /y "%SCRIPT_DIR%fbneo_libretro.dll" "%EXE_DIR%\" > nul
    echo [OK] fbneo_libretro.dll copied
) else (
    echo [WARN] fbneo_libretro.dll not found - place it in %EXE_DIR%\
)

if exist "%SCRIPT_DIR%assets" (
    xcopy /e /i /y /q "%SCRIPT_DIR%assets" "%EXE_DIR%\assets" > nul
    echo [OK] assets copied
)

echo.
echo ============================================
echo  Build complete!
echo  Output: %EXE_DIR%\FBNeoRageX.exe
echo ============================================
echo.
pause
goto :eof

:cdll
if exist "%MB%\%~1" (
    copy /y "%MB%\%~1" "%EXE_DIR%\" > nul
    echo    + %~1
)
goto :eof
