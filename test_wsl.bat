@echo off
echo WSL Diagnostic Tool
echo.

echo [1] wsl.exe location:
where wsl.exe
echo.

echo [2] WSL version:
wsl --version 2>&1
echo.

echo [3] Installed distros:
wsl --list --verbose 2>&1
echo.

echo [4] Ubuntu direct test:
wsl -d Ubuntu --exec echo "Ubuntu OK"
echo   exit code: %errorlevel%
echo.

echo [5] wslpath test:
for /f "delims=" %%i in ('wsl -d Ubuntu --exec wslpath -u "C:\fbneoragex"') do echo   WSL path: %%i
echo.

pause
