@echo off
setlocal
python -X utf8 -B "%~dp0receiver.py"
exit /b %errorlevel%
