@echo off
chcp 65001 >nul
echo Starting Hospital Dashboard...
echo.

set "BASE_DIR=%~dp0"

:: Start Backend
echo Starting Backend Server...
start "Backend" cmd /k "cd /d "%BASE_DIR%Backend" && npm run dev"
timeout /t 3 /nobreak >nul

:: Start Frontend  
echo Starting Frontend Server...
start "Frontend" cmd /k "cd /d "%BASE_DIR%Frontend" && npm run dev"
timeout /t 5 /nobreak >nul

echo.
echo ========================================
echo   Both servers are starting!
echo   Backend:  http://localhost:3000
echo   Frontend: http://localhost:5173
echo ========================================
echo.
echo Press any key to open the browser...
pause >nul
start http://localhost:5173
