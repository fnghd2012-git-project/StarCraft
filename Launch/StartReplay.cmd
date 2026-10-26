@echo off
chcp 65001 >nul
cd /d "%~dp0"
DummyClient.exe --replay-menu
pause
