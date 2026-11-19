@echo off
cd /d "%~dp0"
if not exist local.properties (
  echo sdk.dir=%LOCALAPPDATA%\Android\Sdk> local.properties
)
call gradlew.bat assembleDebug --quiet
if errorlevel 1 exit /b 1
echo Built app\build\outputs\apk\debug\app-debug.apk
exit /b 0
