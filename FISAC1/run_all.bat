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
:: Use caution killing python, only target ones with our script if possible, or just kill all.
:: To be safe and quick for this assignment env, we'll terminate the specific window if we couldn't hide it, 
:: but since we used /b, we might have orphaned it. We'll try to forcefully free the ports.
FOR /F "tokens=5" %%T IN ('netstat -ano ^| findstr :5000') DO taskkill /f /pid %%T >nul 2>&1
FOR /F "tokens=5" %%T IN ('netstat -ano ^| findstr :8080') DO taskkill /f /pid %%T >nul 2>&1

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

:: Link the object files together, enumerating them to avoid wildcard issues
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
start "" /b python scripts\service.py > nul 2>&1
echo [OK] Python DB Backend started.
timeout /t 2 /nobreak > nul

start "" /b server.exe > nul 2>&1
echo [OK] C Server started.

timeout /t 2 /nobreak > nul

echo.
echo [3/3] Opening Frontend...
echo ------------------------------------------------
start "" "http://localhost:5000/"
echo [OK] Frontend opened in default browser.

echo.
echo ================================================
echo   Setup Complete!
echo   - Backend: localhost:5000
echo   - WebSockets: ws://localhost:8080
echo   - GUI: browser
echo ================================================
pause
