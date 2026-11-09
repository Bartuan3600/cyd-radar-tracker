@echo off
cd /d "%~dp0"
echo CYD-Radar-Viewer wird gestartet. Dieses Fenster offen lassen!
echo Browser: Chrome oder Edge (Web Serial). Seite: http://localhost:8000
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0server.ps1"
echo.
echo Server beendet.
pause
