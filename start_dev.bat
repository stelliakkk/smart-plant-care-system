@echo off
REM =====================================================================
REM   Plant Care - one-click dev launcher
REM   Just double-click this file.
REM
REM   Optional switches (open a terminal here and run):
REM       start_dev.bat -WithSim     also start a fake device (no hardware needed)
REM       start_dev.bat -NoBroker    skip the MQTT broker
REM       start_dev.bat -NoBrowser   do not auto-open the browser
REM       start_dev.bat -NetworkProfile <name>  choose a configured edge WiFi profile
REM
REM   NOTE: keep this .bat ASCII-only. cmd.exe reads .bat files using the
REM         system code page, so non-ASCII text here would show up as
REM         garbage. The real logic (and all Chinese messages) lives in
REM         scripts\start_dev.ps1.
REM =====================================================================
chcp 65001 >nul
title Plant Care - Dev Launcher

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\start_dev.ps1" %*

echo.
echo ============ press any key to close this window ============
pause >nul
