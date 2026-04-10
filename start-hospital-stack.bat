@echo off
setlocal EnableExtensions
REM Use OEM code page to avoid UTF-8 multibyte breaking cmd line parsing
chcp 437 >nul

echo ========================================
echo   Hospital Stack Launcher
echo ========================================
echo.

set "ROOT_DIR=%~dp0"
set "BACKEND_DIR=%ROOT_DIR%Hospital Dashboard\Backend"
set "FRONTEND_DIR=%ROOT_DIR%Hospital Dashboard\Frontend"
set "MOSQ_EXE=C:\Program Files\mosquitto\mosquitto.exe"
set "MOSQ_CONF=C:\Program Files\mosquitto\mosquitto.conf"

REM Check and start Mosquitto only if port 1883 is not listening.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=Get-NetTCPConnection -LocalPort 1883 -State Listen -ErrorAction SilentlyContinue; if($p){exit 0}else{exit 1}"
if errorlevel 1 (
  echo [MQTT] Port 1883 is free. Starting Mosquitto broker...
  if exist "%MOSQ_EXE%" (
    if exist "%MOSQ_CONF%" (
      start "Mosquitto Broker" "%MOSQ_EXE%" -c "%MOSQ_CONF%" -v
    ) else (
      start "Mosquitto Broker" "%MOSQ_EXE%" -v
    )
  ) else (
    echo [MQTT] ERROR: mosquitto.exe not found at:
    echo        %MOSQ_EXE%
  )
) else (
  echo [MQTT] Broker already running on port 1883.
)

timeout /t 2 /nobreak >nul

REM MongoDB must be on 27017 (Backend does not start the database).
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=Get-NetTCPConnection -LocalPort 27017 -State Listen -ErrorAction SilentlyContinue; if($p){exit 0}else{exit 1}"
if errorlevel 1 (
  echo [DB]  WARNING: No listener on port 27017 - Backend needs MongoDB.
  echo       Install MongoDB Community, start service MongoDB, or use Atlas + MONGO_URI in Backend\.env
  echo       Quick Docker:  docker run -d -p 27017:27017 --name hospital-mongo mongo:7
  echo.
)

REM Check and start backend only if port 3000 is not listening.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=Get-NetTCPConnection -LocalPort 3000 -State Listen -ErrorAction SilentlyContinue; if($p){exit 0}else{exit 1}"
if errorlevel 1 (
  echo [WEB] Starting Backend on port 3000...
  start "Hospital Backend" cmd /k "cd /d ""%BACKEND_DIR%"" && npm run dev"
) else (
  echo [WEB] Backend already running on port 3000.
)

timeout /t 2 /nobreak >nul

REM Check and start frontend only if port 5173 is not listening.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$p=Get-NetTCPConnection -LocalPort 5173 -State Listen -ErrorAction SilentlyContinue; if($p){exit 0}else{exit 1}"
if errorlevel 1 (
  echo [WEB] Starting Frontend on port 5173...
  start "Hospital Frontend" cmd /k "cd /d ""%FRONTEND_DIR%"" && npm run dev"
) else (
  echo [WEB] Frontend already running on port 5173.
)

echo.
echo ========================================
echo   Done. Services status:
echo   MQTT Broker : tcp/1883
echo   Backend API : http://localhost:3000
echo   Frontend dev: http://localhost:5173  (npm run dev - hot reload)
echo   Dashboard   : http://localhost:3000  (SPA from Frontend/dist via Backend)
echo ========================================
echo.
echo Browser will open in 3 seconds...
timeout /t 3 /nobreak >nul
start http://localhost:3000

endlocal
