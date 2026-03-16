@echo off
setlocal
chcp 65001 >nul

echo ========================================
echo   Hospital Stack Stopper
echo ========================================
echo.

REM Stop process by listening port (1883 MQTT, 3000 Backend, 5173 Frontend)
for %%P in (1883 3000 5173) do (
  powershell -NoProfile -ExecutionPolicy Bypass -Command "$port=%%P; $conns=Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue; if(-not $conns){Write-Host ('[STOP] Port ' + $port + ' has no listener.'); exit 0}; $pids=$conns | Select-Object -ExpandProperty OwningProcess -Unique; foreach($procId in $pids){ try{$proc=Get-Process -Id $procId -ErrorAction Stop; Stop-Process -Id $procId -Force -ErrorAction Stop; Write-Host ('[STOP] Killed PID ' + $procId + ' (' + $proc.ProcessName + ') on port ' + $port + '.')} catch {Write-Host ('[STOP] Failed to stop PID ' + $procId + ' on port ' + $port + '.')}}"
)

echo.
echo ========================================
echo   Stop command finished.
echo ========================================
echo.

endlocal
