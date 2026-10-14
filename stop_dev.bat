@echo off
REM =====================================================================
REM   Plant Care - stop all dev services (broker / backend / frontend)
REM   Just double-click this file.
REM =====================================================================
chcp 65001 >nul
title Plant Care - Stop Dev Services

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\stop_dev.ps1"

echo ============ press any key to close this window ============
pause >nul
