@echo off
rem Debug run: profile MirEfir-native (real profile MirEfir stays untouched)
set MIREFIR_PROFILE=native
cd /d "%~dp0"
taskkill /F /IM mirefir.exe >nul 2>&1
taskkill /F /IM mpv.exe >nul 2>&1
call build.bat
if errorlevel 1 exit /b 1
start "" "%~dp0bin\mirefir.exe"
