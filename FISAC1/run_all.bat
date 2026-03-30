@echo off
title GeoSync - Location Sharing Server
setlocal

set NO_PAUSE=0
if /I "%~1"=="--no-pause" set NO_PAUSE=1
set WIN_TIMEOUT=%SystemRoot%\System32\timeout.exe
set SOCKOPT_PROFILE=tuned
if /I "%~2"=="baseline" set SOCKOPT_PROFILE=baseline
if /I "%~2"=="tuned" set SOCKOPT_PROFILE=tuned

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
"%WIN_TIMEOUT%" /t 1 /nobreak > nul

echo [PRECHECK] Validating toolchain...
echo ------------------------------------------------
where mingw32-make >nul 2>&1
if %errorlevel% neq 0 (
    where gcc >nul 2>&1
    if %errorlevel% neq 0 (
        echo [FATAL] GCC/MinGW not found in PATH.
        echo [HINT] Install MSYS2 MinGW-w64 and add gcc + mingw32-make to PATH.
        goto :end
    )
)

echo [1/3] Compiling the C WebSocket Server...
echo ------------------------------------------------

mingw32-make clean
mingw32-make

if %errorlevel% neq 0 (
    echo [FATAL] Compilation/Linking failed!
    goto :end
)

if not exist server.exe (
    echo [FATAL] server.exe not found after compilation!
    goto :end
)

echo [OK] Compilation successful!
echo.

echo [2/3] Starting Services...
echo ------------------------------------------------

:: Check for python interpreter
set PYTHON_CMD=python
where python >nul 2>&1
if %errorlevel% neq 0 (
    where py >nul 2>&1
    if %errorlevel% equ 0 set PYTHON_CMD=py -3
)

echo [INFO] Using %PYTHON_CMD% to start backend.
echo [INFO] Socket profile: %SOCKOPT_PROFILE%

%PYTHON_CMD% -c "import flask" >nul 2>&1
if %errorlevel% neq 0 (
    echo [FATAL] Python package 'flask' is not installed.
    echo [HINT] Run: %PYTHON_CMD% -m pip install -r scripts\requirements.txt
    goto :end
)

:: Redirect output to HIDDEN files to keep the desktop clean as requested
del .backend.log >nul 2>&1
del .server.log >nul 2>&1

:: Launch processes detached and minimized
start "" /min cmd /c "%PYTHON_CMD% scripts\service.py > .backend.log 2>&1"
start "" /min cmd /c "set FISAC_SOCKOPTS_PROFILE=%SOCKOPT_PROFILE% && server.exe > .server.log 2>&1"
echo [OK] Background services launched.

:: Give services time to start up
"%WIN_TIMEOUT%" /t 2 /nobreak > nul

call :wait_for_port 5000 12
if %errorlevel% neq 0 (
    echo [FATAL] Python backend did not start on port 5000.
    call :show_logs
    goto :end
)

call :wait_for_port 8080 12
if %errorlevel% neq 0 (
    echo [FATAL] C WebSocket server did not start on port 8080.
    call :show_logs
    goto :end
)

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

:end
if "%NO_PAUSE%"=="0" pause
endlocal
goto :eof

:wait_for_port
set PORT=%~1
set RETRIES=%~2
:wait_loop
netstat -ano | findstr LISTENING | findstr /R /C:":%PORT% " >nul 2>&1
if %errorlevel% equ 0 exit /b 0
set /a RETRIES-=1
if %RETRIES% leq 0 exit /b 1
"%WIN_TIMEOUT%" /t 1 /nobreak >nul
goto :wait_loop

:show_logs
echo ---------------- BACKEND LOG (tail) ----------------
if exist .backend.log type .backend.log
echo ---------------- SERVER LOG (tail) -----------------
if exist .server.log type .server.log
exit /b 0
