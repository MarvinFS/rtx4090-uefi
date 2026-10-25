@echo off
rem RTX 4090 pre-boot unlock: checks the install and whether this Windows session is unlocked. Changes nothing.
net session >nul 2>&1 || (powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'" & exit /b)
cd /d "%~dp0"
set BENCH=
choice /c YN /m "Also run the one-minute GPU benchmark that measures the unlock"
if not errorlevel 2 set BENCH=-Benchmark
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-Gpu4090Unlock.ps1" %BENCH%
echo.
pause
