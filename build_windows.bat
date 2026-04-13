@echo off
if "%1"=="RERUN" goto BUILD
start "" cmd /k ""%~f0" RERUN"
exit

:BUILD
setlocal enabledelayedexpansion
title FBNeoRageX Build
cd /d "%~dp0"

echo ============================================
echo  FBNeoRageX - Windows Build (onefile)
echo  Dir: %CD%
echo ============================================
echo.

echo [1] Checking Python...
where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found. Install Python 3.10+ and add to PATH.
    goto END
)
python --version
echo.

echo [2] Installing dependencies...
python -m pip install --upgrade pip --quiet
python -m pip install pyinstaller pyside6 pyopengl pyopengl-accelerate pillow numpy sounddevice --quiet
if errorlevel 1 (
    echo [ERROR] Dependency install failed.
    goto END
)
python -m PyInstaller --version
echo [OK] Dependencies ready.
echo.

echo [3] Checking source files...
if not exist "FBNeoRageX_v1.7.py" (
    echo [ERROR] FBNeoRageX_v1.7.py not found in %CD%
    goto END
)
if not exist "FBNeoRageX_Windows.spec" (
    echo [ERROR] FBNeoRageX_Windows.spec not found.
    goto END
)
echo [OK] Source files found.
echo.

if not exist "fbneo_libretro.dll" (
    echo [WARN] fbneo_libretro.dll not found - users must place it next to exe.
    echo.
) else (
    echo [OK] fbneo_libretro.dll found - will be copied to output folder.
)

echo [4] Cleaning previous build...
if exist "dist\FBNeoRageX.exe"  del /q "dist\FBNeoRageX.exe"
if exist "build\FBNeoRageX"     rmdir /s /q "build\FBNeoRageX"
echo [OK] Done.
echo.

echo [5] Running PyInstaller (onefile)...
echo.
python -m PyInstaller FBNeoRageX_Windows.spec --noconfirm
echo.

if errorlevel 1 (
    echo [ERROR] PyInstaller FAILED.
    goto END
)
if not exist "dist\FBNeoRageX.exe" (
    echo [ERROR] dist\FBNeoRageX.exe not found after build.
    goto END
)
echo [OK] Build complete: dist\FBNeoRageX.exe
echo.

echo [6] Preparing output folder...
set OUT=dist\Release
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"

copy /y "dist\FBNeoRageX.exe" "%OUT%\FBNeoRageX.exe" >nul
echo [OK] FBNeoRageX.exe copied.

if exist "fbneo_libretro.dll" (
    copy /y "fbneo_libretro.dll" "%OUT%\fbneo_libretro.dll" >nul
    echo [OK] fbneo_libretro.dll copied.
)

echo FBNeoRageX > "%OUT%\README.txt"
echo. >> "%OUT%\README.txt"
echo REQUIRED: Place fbneo_libretro.dll next to FBNeoRageX.exe >> "%OUT%\README.txt"
echo Then run FBNeoRageX.exe - no installation needed. >> "%OUT%\README.txt"
echo. >> "%OUT%\README.txt"
echo Get fbneo_libretro.dll from RetroArch Core Downloader (search: FinalBurn Neo) >> "%OUT%\README.txt"
echo.

echo [7] Creating ZIP...
set ZIP_NAME=FBNeoRageX_v1.7_Windows.zip
if exist "%ZIP_NAME%" del "%ZIP_NAME%"
powershell -NoProfile -Command "Compress-Archive -Path '%OUT%\*' -DestinationPath '%ZIP_NAME%' -Force"
if exist "%ZIP_NAME%" (
    echo [OK] ZIP ready: %ZIP_NAME%
) else (
    echo [WARN] ZIP failed. Files ready at %OUT%\
)

echo.
echo ============================================
echo  BUILD COMPLETE
echo  Single EXE : dist\Release\FBNeoRageX.exe
echo  ZIP        : %ZIP_NAME%
echo.
echo  How to use:
echo   1. Copy FBNeoRageX.exe anywhere
echo   2. Place fbneo_libretro.dll next to it
echo   3. Run FBNeoRageX.exe  (no install needed)
echo ============================================

:END
echo.
echo Press any key to close...
pause >nul
exit
