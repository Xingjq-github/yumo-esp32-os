@echo off
setlocal EnableExtensions DisableDelayedExpansion
if not exist "%~dp0runtime\python.exe" (
  echo Missing portable runtime. Extract the complete firmware package first.
  if "%~1"=="" pause
  exit /b 1
)
if not exist "%~dp0portable_flash.py" (
  echo Missing portable_flash.py. Extract the complete firmware package first.
  if "%~1"=="" pause
  exit /b 1
)
"%~dp0runtime\python.exe" -I -X utf8 -B "%~dp0portable_flash.py" %*
set "YUMO_FLASH_EXIT=%errorlevel%"
if "%~1"=="" pause
exit /b %YUMO_FLASH_EXIT%
