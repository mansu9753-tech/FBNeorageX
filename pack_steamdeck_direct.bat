@echo off
setlocal
cd /d "%~dp0"

echo ============================================
echo  FBNeoRageX - Steam Deck Package (Direct)
echo ============================================
echo.

if not exist "fbneo_libretro.so" (
    echo [ERROR] fbneo_libretro.so not found.
    pause
    exit /b 1
)
if not exist "FBNeoRageX_v1.7.py" (
    echo [ERROR] FBNeoRageX_v1.7.py not found.
    pause
    exit /b 1
)
if not exist "run_steamdeck.sh" (
    echo [ERROR] run_steamdeck.sh not found.
    pause
    exit /b 1
)

set OUT=dist_steamdeck_direct
set APP=%OUT%\FBNeoRageX

echo Cleaning previous output...
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%APP%"

echo Copying files...
copy "FBNeoRageX_v1.7.py"   "%APP%\" >nul
copy "fbneo_libretro.so"     "%APP%\" >nul
copy "config.json"           "%APP%\" >nul
copy "game_names_db.json"    "%APP%\" >nul
copy "run_steamdeck.sh"      "%APP%\run.sh" >nul

if exist "assets"   xcopy /e /q "assets"   "%APP%\assets\"   >nul
if exist "previews" xcopy /e /q "previews" "%APP%\previews\" >nul
if exist "roms"     xcopy /e /q "roms"     "%APP%\roms\"     >nul
if exist "cheats"   xcopy /e /q "cheats"   "%APP%\cheats\"   >nul

echo.
echo ============================================
echo  DONE - Output: %APP%\
echo ============================================
echo.
echo === Steam Deck Instructions ===
echo.
echo 1. Copy the "%APP%" folder to Steam Deck
echo    Target: /home/deck/FBNeoRageX/
echo.
echo 2. On Steam Deck, run ONCE in terminal:
echo    chmod +x ~/FBNeoRageX/run.sh
echo.
echo 3. Register as Non-Steam Game:
echo    Steam - Add Non-Steam Game - Browse
echo    Select: /home/deck/FBNeoRageX/run.sh
echo.
echo 4. Launch! (First run auto-installs PySide6 - takes ~5 min)
echo    After that: instant launch.
echo.
dir "%APP%\" /b
echo.
pause
