@echo off
rem Quick start: double-click this. It checks what you need, asks before
rem installing anything, builds the recompiler, recompiles your ROM and leaves
rem a "Play <game>.cmd" launcher next to this file. Re-run it to resume.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\setup.ps1" %*
echo.
pause
