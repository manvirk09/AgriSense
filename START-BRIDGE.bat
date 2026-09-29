@echo off
setlocal
cd /d "%~dp0"
echo.
echo ================================================
echo Smart Crop Monitoring - Wokwi Bridge
echo ================================================
echo.
where py >nul 2>&1
if %errorlevel%==0 (
    set "PYTHON=py"
) else (
    set "PYTHON=python"
)

echo Installing/updating bridge packages...
%PYTHON% -m pip install -r requirements.txt
if errorlevel 1 (
    echo.
    echo Could not install the Python packages.
    echo Make sure Python is installed and try again.
    pause
    exit /b 1
)

echo.
echo Starting bridge on ws://127.0.0.1:8787
%PYTHON% bridge.py
pause
