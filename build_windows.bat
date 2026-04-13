@echo off
if "%1"=="RERUN" goto BUILD
start "" cmd /k ""%~f0" RERUN"
exit

:BUILD
setlocal enabledelayedexpansion
title FBNeoRageX Build
cd /d "%~dp0"

echo ============================================
echo  FBNeoRageX - Windows Build
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
    echo [OK] fbneo_libretro.dll found - will be copied to output.
)

echo [4] Cleaning previous build...
if exist "dist\FBNeoRageX"  rmdir /s /q "dist\FBNeoRageX"
if exist "build\FBNeoRageX" rmdir /s /q "build\FBNeoRageX"
echo [OK] Done.
echo.

echo [5] Running PyInstaller (onedir)...
echo.
python -m PyInstaller FBNeoRageX_Windows.spec --noconfirm
echo.

if errorlevel 1 (
    echo [ERROR] PyInstaller FAILED.
    goto END
)
if not exist "dist\FBNeoRageX\FBNeoRageX.exe" (
    echo [ERROR] dist\FBNeoRageX\FBNeoRageX.exe not found after build.
    goto END
)
echo [OK] Build complete: dist\FBNeoRageX\FBNeoRageX.exe
echo.

echo [6] Copying extra files...
if exist "fbneo_libretro.dll" (
    copy /y "fbneo_libretro.dll" "dist\FBNeoRageX\fbneo_libretro.dll" >nul
    echo [OK] fbneo_libretro.dll copied.
)

echo [OK] Writing README.txt...
echo FBNeoRageX > "dist\FBNeoRageX\README.txt"
echo. >> "dist\FBNeoRageX\README.txt"
echo REQUIRED: Place fbneo_libretro.dll next to FBNeoRageX.exe >> "dist\FBNeoRageX\README.txt"
echo Then run FBNeoRageX.exe - no installation needed. >> "dist\FBNeoRageX\README.txt"
echo. >> "dist\FBNeoRageX\README.txt"
echo Get fbneo_libretro.dll from RetroArch Core Downloader (search: FinalBurn Neo) >> "dist\FBNeoRageX\README.txt"
echo.

echo [7] Creating ZIP...
set ZIP_NAME=FBNeoRageX_v1.7_Windows.zip
if exist "%ZIP_NAME%" del "%ZIP_NAME%"
powershell -NoProfile -Command "Compress-Archive -Path 'dist\FBNeoRageX' -DestinationPath '%ZIP_NAME%' -Force"
if exist "%ZIP_NAME%" (
    echo [OK] ZIP ready: %ZIP_NAME%
) else (
    echo [WARN] ZIP failed. Folder ready at dist\FBNeoRageX\
)

echo.
echo ============================================
echo  BUILD COMPLETE
echo  Output folder : dist\FBNeoRageX\
echo  Output ZIP    : %ZIP_NAME%
echo.
echo  How to use:
echo   1. Extract ZIP
echo   2. Place fbneo_libretro.dll next to FBNeoRageX.exe
echo   3. Run FBNeoRageX.exe  (no install needed)
echo ============================================

:END
echo.
echo Press any key to close...
pause >nul
exit
