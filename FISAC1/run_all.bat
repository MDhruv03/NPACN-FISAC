@echo off
title FISAC1 Location Tracker Setup
echo ==============================================
echo Building the C WebSocket Server...
echo ==============================================

mingw32-make
if %errorlevel% neq 0 (
    echo [!] mingw32-make failed. Falling back to direct GCC compilation...
    gcc -Wall -Wextra -I./include -o server src/*.c -lm
    if %errorlevel% neq 0 (
        echo [!] Compilation failed! Please ensure GCC is installed.
        pause
        exit /b %errorlevel%
    )
)

echo.
echo ==============================================
echo Starting Python Data Service (Flask)...
echo ==============================================
start "Python Backend Service" cmd /k "cd scripts && python service.py"
echo [OK] Python service launched in a new window.

timeout /t 2 /nobreak > nul

echo.
echo ==============================================
echo Starting C WebSocket Server...
echo ==============================================
start "C WebSocket Server" cmd /k "server.exe"
echo [OK] C server launched in a new window.

timeout /t 1 /nobreak > nul

echo.
echo ==============================================
echo Opening Frontend Web App...
echo ==============================================
start "" "frontend\index.html"
echo [OK] Frontend launched in default browser.

echo.
echo Setup Complete! All services should now be running.
echo To safely shut down, individually close the new console windows.
pause
