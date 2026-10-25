@echo off
rem RTX 4090 pre-boot unlock: shows the removal plan, asks, then removes the boot entry and the files; the partition stays.
net session >nul 2>&1 || (powershell -NoProfile -Command "Start-Process -Verb RunAs -FilePath '%~f0'" & exit /b)
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-Gpu4090Unlock.ps1"
if errorlevel 1 goto done
echo.
choice /c YN /m "Remove it now"
if errorlevel 2 goto done
rem One line: the next step may delete this file (ProgramData copy), so cmd must not read further.
cd /d "%TEMP%" & powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Uninstall-Gpu4090Unlock.ps1" -Apply & echo. & pause & exit /b
:done
echo.
pause
