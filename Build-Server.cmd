@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tooling\server\Build-Server.ps1" %*
exit /b %errorlevel%
