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

:: ── [1] Python 확인 ──────────────────────────────────────
echo [1] Python check...
where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python not found. Install Python 3.10+ with PATH option.
    goto END
)
python --version
echo.

:: ── [2] 의존성 설치 ──────────────────────────────────────
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

:: ── [3] 소스 파일 확인 ───────────────────────────────────
echo [3] Source files check...
if not exist "FBNeoRageX_v1.7.py" (
    echo [ERROR] FBNeoRageX_v1.7.py not found.
    goto END
)
if not exist "FBNeoRageX_Windows.spec" (
    echo [ERROR] FBNeoRageX_Windows.spec not found.
    goto END
)
echo [OK] Source files found.
echo.

:: fbneo_libretro.dll 은 번들에 포함하지 않으므로 빌드 시 없어도 됨
if not exist "fbneo_libretro.dll" (
    echo [WARN] fbneo_libretro.dll not found here.
    echo        It will NOT be bundled - users must place it next to FBNeoRageX.exe
    echo.
) else (
    echo [OK] fbneo_libretro.dll found - will be included in ZIP.
)

:: ── [4] 이전 빌드 정리 ───────────────────────────────────
echo [4] Cleaning previous build...
if exist "dist\FBNeoRageX"  rmdir /s /q "dist\FBNeoRageX"
if exist "build\FBNeoRageX" rmdir /s /q "build\FBNeoRageX"
echo [OK] Done.
echo.

:: ── [5] PyInstaller 빌드 ─────────────────────────────────
echo [5] Building with PyInstaller (onedir)...
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

:: ── [6] dll 복사 (있는 경우) ─────────────────────────────
echo [6] Copying extra files...
if exist "fbneo_libretro.dll" (
    copy /y "fbneo_libretro.dll" "dist\FBNeoRageX\fbneo_libretro.dll" >nul
    echo [OK] fbneo_libretro.dll copied.
)

:: README.txt 생성 (dll 받는 곳 안내)
(
echo FBNeoRageX
echo ==========
echo.
echo [필수] fbneo_libretro.dll 을 이 폴더에 넣고 FBNeoRageX.exe 를 실행하세요.
echo.
echo fbneo_libretro.dll 다운로드:
echo  - RetroArch 코어 다운로더에서 "FinalBurn Neo" 검색
echo  - 또는 https://github.com/finalburnneo/FBNeo 빌드
echo.
echo [REQUIRED] Place fbneo_libretro.dll in this folder, then run FBNeoRageX.exe
echo.
echo Get fbneo_libretro.dll from:
echo  - RetroArch Core Downloader - search "FinalBurn Neo"
echo  - Or build from https://github.com/finalburnneo/FBNeo
) > "dist\FBNeoRageX\README.txt"
echo [OK] README.txt created.
echo.

:: ── [7] ZIP 패키징 ───────────────────────────────────────
echo [7] Creating ZIP package...
set ZIP_NAME=FBNeoRageX_v1.7_Windows.zip
if exist "%ZIP_NAME%" del "%ZIP_NAME%"

powershell -NoProfile -Command ^
    "Compress-Archive -Path 'dist\FBNeoRageX' -DestinationPath '%ZIP_NAME%' -Force"

if exist "%ZIP_NAME%" (
    echo [OK] ZIP ready: %ZIP_NAME%
) else (
    echo [WARN] ZIP failed. Folder is ready at dist\FBNeoRageX\
)

echo.
echo ============================================
echo  BUILD COMPLETE
echo.
echo  배포 폴더: dist\FBNeoRageX\
echo  배포 ZIP:  %ZIP_NAME%
echo.
echo  사용자 실행 방법:
echo   1. ZIP 압축 해제
echo   2. fbneo_libretro.dll 을 FBNeoRageX.exe 옆에 배치
echo   3. FBNeoRageX.exe 실행 (설치 불필요)
echo ============================================

:END
echo.
echo Press any key to close...
pause >nul
exit
