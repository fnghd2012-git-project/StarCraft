@echo off
chcp 65001 >nul
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0StartBattle.ps1" -PerServer 500 -Seconds 240
pause
