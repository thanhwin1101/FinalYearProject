@echo off
echo Starting Hospital Dashboard...
echo.

:: Start Backend
echo Starting Backend Server...
start "Backend" cmd /c "cd /d \"c:\Users\ironc\Desktop\Hospital\Hospital Dashboard\Backend\" && node src/index.js"
timeout /t 3 /nobreak >nul

:: Start Frontend
echo Starting Frontend Server...
start "Frontend" cmd /c "cd /d \"c:\Users\ironc\Desktop\Hospital\Hospital Dashboard\Frontend\" && npm run dev"
timeout /t 3 /nobreak >nul

echo.
echo Both servers are starting!
echo Backend: http://localhost:3000
echo Frontend: http://localhost:5173
echo.
echo Press any key to open the browser...
pause >nul
start http://localhost:5173
