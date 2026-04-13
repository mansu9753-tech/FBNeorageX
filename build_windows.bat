@echo off
if "%1"=="RERUN" goto BUILD
start "" cmd /k ""%~f0" RERUN"
exit

:BUILD
setlocal enabledelayedexpansion
title FBNeoRageX Build
cd /d "%~dp0"

echo ============================================
echo  FBNeoRageX v1.7 - Windows Build
echo  Dir: %CD%
echo ============================================
echo.

echo [1] Python check...
where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found. Install Python 3.11+ with PATH option.
    goto END
)
python --version
echo.

echo [2] PyInstaller check...
python -m PyInstaller --version >nul 2>&1
if errorlevel 1 (
    echo PyInstaller not found. Upgrading pip and installing...
    python -m pip install --upgrade pip --quiet
    python -m pip install pyinstaller --quiet
    if errorlevel 1 (
        echo [ERROR] PyInstaller install failed.
        echo   Try manually: python -m pip install pyinstaller
        goto END
    )
)
python -m PyInstaller --version

echo [2b] Installing dependencies if missing...
python -m pip install pyside6 pillow numpy sounddevice --quiet
echo.

echo [3] Source files check...
if not exist "FBNeoRageX_v1.7.py" (
    echo [ERROR] FBNeoRageX_v1.7.py not found in %CD%
    goto END
)
if not exist "FBNeoRageX_Windows.spec" (
    echo [ERROR] FBNeoRageX_Windows.spec not found.
    goto END
)
if not exist "fbneo_libretro.dll" (
    echo [ERROR] fbneo_libretro.dll not found in %CD%
    goto END
)
echo [OK] All files found.
echo.

echo [4] Cleaning old build...
if exist "dist\FBNeoRageX.exe" del /q "dist\FBNeoRageX.exe"
if exist "dist\FBNeoRageX"     rmdir /s /q "dist\FBNeoRageX"
if exist "build\FBNeoRageX"    rmdir /s /q "build\FBNeoRageX"
echo [OK] Done.
echo.

echo [5] Running PyInstaller...
echo.
python -m PyInstaller FBNeoRageX_Windows.spec --noconfirm
echo.

if errorlevel 1 (
    echo [ERROR] PyInstaller FAILED.
    goto END
)

if not exist "dist\FBNeoRageX.exe" (
    echo [ERROR] dist\FBNeoRageX.exe not found.
    goto END
)
echo [OK] EXE ready: dist\FBNeoRageX.exe
echo.

echo [6] Copying fbneo_libretro.dll next to EXE...
copy /y "fbneo_libretro.dll" "dist\fbneo_libretro.dll" >nul
if exist "dist\fbneo_libretro.dll" (
    echo [OK] fbneo_libretro.dll copied to dist\
) else (
    echo [ERROR] dll copy failed.
    goto END
)

echo [6b] Copying game_names_db.json next to EXE...
if exist "game_names_db.json" (
    copy /y "game_names_db.json" "dist\game_names_db.json" >nul
    echo [OK] game_names_db.json copied to dist\
) else (
    echo [WARN] game_names_db.json not found - game names DB will be empty
)

echo [7] Creating ZIP...
if exist "FBNeoRageX_v1.7_Windows.zip" del "FBNeoRageX_v1.7_Windows.zip"
powershell -NoProfile -Command "Compress-Archive -Path 'dist\FBNeoRageX.exe','dist\fbneo_libretro.dll','dist\game_names_db.json' -DestinationPath 'FBNeoRageX_v1.7_Windows.zip' -Force"
if exist "FBNeoRageX_v1.7_Windows.zip" (
    echo [OK] ZIP ready: FBNeoRageX_v1.7_Windows.zip
) else (
    echo [WARN] ZIP failed. Files are in dist\
)

echo.
echo ============================================
echo  BUILD COMPLETE
echo  dist\FBNeoRageX.exe
echo  dist\fbneo_libretro.dll
echo  dist\game_names_db.json  [editable game name DB]
echo  NOTE: all 3 files must stay in the same folder
echo ============================================

:END
echo.
echo Press any key to close...
pause >nul
exit
