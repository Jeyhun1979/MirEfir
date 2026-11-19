@echo off
rem Собирает Windows Setup: dist\MirEfir-Setup-<версия>.exe (версия из src\app.h).
rem Имя ищет автообновление (.exe с "setup" в названии) — менять формат нельзя.
setlocal
cd /d "%~dp0"
call build.bat
if errorlevel 1 exit /b 1

set VER=
for /f "tokens=3 delims= " %%a in ('findstr /b /c:"#define MIREFIR_VERSION" src\app.h') do set VER=%%~a
if "%VER%"=="" (
  echo MIREFIR_VERSION not found in src\app.h
  exit /b 1
)
if not exist bin\mpv\mpv.exe (
  echo bin\mpv\mpv.exe is missing: run fetch-mpv.ps1
  exit /b 1
)

set "NSIS="
where makensis >nul 2>&1 && set "NSIS=makensis"
set "PF86=%ProgramFiles(x86)%"
if not defined NSIS if exist "%PF86%\NSIS\makensis.exe" set "NSIS=%PF86%\NSIS\makensis.exe"
if not defined NSIS for /r "%LOCALAPPDATA%\electron-builder\Cache" %%d in (makensis.exe) do if exist "%%d" set "NSIS=%%d"
if not defined NSIS (
  echo makensis not found. Install NSIS 3 from https://nsis.sourceforge.io and retry.
  exit /b 1
)

if exist dist\stage rmdir /s /q dist\stage
mkdir dist\stage\mpv
copy /Y bin\mirefir.exe dist\stage\MirEfir.exe >nul
xcopy /E /I /Y /Q bin\mpv dist\stage\mpv >nul
"%NSIS%" /V2 /DVERSION=%VER% "/DSTAGE=%~dp0dist\stage" "/DOUT=%~dp0dist\MirEfir-Setup-%VER%.exe" installer\mirefir.nsi
if errorlevel 1 exit /b 1
rmdir /s /q dist\stage
echo Done: dist\MirEfir-Setup-%VER%.exe
exit /b 0
