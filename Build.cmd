@echo off
setlocal
python -X utf8 -B "%~dp0tooling\firmware\build_firmware.py" %*
exit /b %errorlevel%
