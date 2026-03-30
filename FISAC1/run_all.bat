@echo off
title GeoSync - Location Sharing Server
echo ================================================
echo   GeoSync - Real-Time Location Sharing Server
echo   FISAC Assignment - Group 12
echo ================================================
echo.

:: Ensure the script runs in its own directory
cd /d "%~dp0"

echo [0/3] Stopping existing services...
echo ------------------------------------------------
taskkill /f /im server.exe >nul 2>&1
FOR /F "tokens=5" %%T IN ('netstat -ano ^| findstr :5000') DO taskkill /f /pid %%T >nul 2>&1
FOR /F "tokens=5" %%T IN ('netstat -ano ^| findstr :8080') DO taskkill /f /pid %%T >nul 2>&1
timeout /t 1 /nobreak > nul

echo [1/3] Compiling the C WebSocket Server...
echo ------------------------------------------------

if not exist obj mkdir obj
gcc -Wall -Wextra -I./include -g -c src/main.c -o obj/main.o
gcc -Wall -Wextra -I./include -g -c src/server.c -o obj/server.o
gcc -Wall -Wextra -I./include -g -c src/socket.c -o obj/socket.o
gcc -Wall -Wextra -I./include -g -c src/network.c -o obj/network.o
gcc -Wall -Wextra -I./include -g -c src/websocket.c -o obj/websocket.o
gcc -Wall -Wextra -I./include -g -c src/protocol.c -o obj/protocol.o
gcc -Wall -Wextra -I./include -g -c src/http_client.c -o obj/http_client.o
gcc -Wall -Wextra -I./include -g -c src/sha1.c -o obj/sha1.o
gcc -Wall -Wextra -I./include -g -c src/base64.c -o obj/base64.o
gcc -Wall -Wextra -I./include -g -c src/cJSON.c -o obj/cJSON.o

:: Link (database.c is Python-backed; no sqlite needed in the C binary)
gcc -o server.exe obj/main.o obj/server.o obj/socket.o obj/network.o obj/websocket.o obj/protocol.o obj/http_client.o obj/sha1.o obj/base64.o obj/cJSON.o -lws2_32

if %errorlevel% neq 0 (
    echo [FATAL] Compilation/Linking failed!
    pause
    exit /b 1
)

if not exist server.exe (
    echo [FATAL] server.exe not found after compilation!
    pause
    exit /b 1
)

echo [OK] Compilation successful!
echo.

echo [2/3] Starting Services...
echo ------------------------------------------------

:: Check for python interpreter
set PYTHON_CMD=python
where py >nul 2>&1
if %errorlevel% equ 0 set PYTHON_CMD=py

echo [INFO] Using %PYTHON_CMD% to start backend.

:: Redirect output to HIDDEN files to keep the desktop clean as requested
del .backend.log >nul 2>&1
del .server.log >nul 2>&1

:: Generate VBScript to launch processes completely detached from this console
echo Set WshShell = CreateObject("WScript.Shell") > start_bg.vbs
echo WshShell.Run "cmd /c ""%PYTHON_CMD% scripts\service.py > .backend.log 2>&1""", 0, False >> start_bg.vbs
echo WshShell.Run "cmd /c ""server.exe > .server.log 2>&1""", 0, False >> start_bg.vbs

:: Execute the VBScript
wscript start_bg.vbs
echo [OK] Background services launched completely silently.

:: Give services time to start up
timeout /t 3 /nobreak > nul

echo.
echo [3/3] Opening Frontend...
echo ------------------------------------------------
:: Open Flask-served URL
start "" "http://localhost:5000/"
echo [OK] Frontend opened in default browser.

echo.
echo ================================================
echo   Setup Complete!
echo   Flask backend: http://localhost:5000
echo   WebSocket:     ws://localhost:8080
echo   Login:         user1/pass1 or user2/pass2
echo ================================================
echo.
echo TIP: If login sticks on "Connecting", check the Python window.
echo TIP: If WebSocket fails, check the C Server window.
pause
