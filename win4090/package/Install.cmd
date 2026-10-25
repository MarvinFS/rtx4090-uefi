@echo off
rem RTX 4090 pre-boot unlock: shows the install plan, asks, then installs and verifies.
net session >nul 2>&1 || (powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'" & exit /b)
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Gpu4090Unlock.ps1" %*
if errorlevel 1 goto done
echo.
choice /c YN /m "Install it now (creates the files, the partition if planned, and the firmware boot entry)"
if errorlevel 2 goto done
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Install-Gpu4090Unlock.ps1" %* -Apply
:done
echo.
pause
