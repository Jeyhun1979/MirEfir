@echo off
setlocal
cd /d "%~dp0"
if not exist bin mkdir bin
if not exist tools mkdir tools
set ZIGDIR=%~dp0tools\zig
set ZIG=%ZIGDIR%\zig.exe
if not exist "%ZIG%" (
  echo Downloading portable C compiler...
  if not exist "%~dp0tools\zig.zip" curl.exe -L --fail -o "%~dp0tools\zig.zip" https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip
  if exist "%~dp0tools\zig-unpack" rmdir /s /q "%~dp0tools\zig-unpack"
  mkdir "%~dp0tools\zig-unpack"
  tar -xf "%~dp0tools\zig.zip" -C "%~dp0tools\zig-unpack"
  move "%~dp0tools\zig-unpack\zig-windows-x86_64-0.13.0" "%ZIGDIR%"
  if not exist "%ZIG%" (
    echo Failed to get compiler
    exit /b 1
  )
)
if not exist "%~dp0vendor\mpv\mpv.exe" (
  echo Preparing player in vendor\mpv
  powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0fetch-mpv.ps1"
)
if exist "%~dp0vendor\mpv\mpv.exe" (
  if not exist "%~dp0bin\mpv" mkdir "%~dp0bin\mpv"
  copy /Y "%~dp0vendor\mpv\mpv.exe" "%~dp0bin\mpv\mpv.exe" >nul
  if exist "%~dp0vendor\mpv\vulkan-1.dll" copy /Y "%~dp0vendor\mpv\vulkan-1.dll" "%~dp0bin\mpv\vulkan-1.dll" >nul
  if exist "%~dp0vendor\mpv\d3dcompiler_43.dll" copy /Y "%~dp0vendor\mpv\d3dcompiler_43.dll" "%~dp0bin\mpv\d3dcompiler_43.dll" >nul
)
"%ZIG%" cc src\main.c src\json.c src\profile.c src\http.c src\playlist.c src\player.c src\device.c src\shell.c src\epg.c src\catchup.c src\rec.c src\voice.c src\i18n.c src\logo.c src\update.c src\tinf\tinflate.c src\tinf\tinfgzip.c src\tinf\tinfcrc32.c src\mirefir.rc -I src\tinf -o bin\mirefir.exe -target x86_64-windows-gnu -luser32 -lgdi32 -lshell32 -lcomdlg32 -lole32 -luuid -ldxgi -ladvapi32 -lwindowscodecs -lwinhttp -lwinmm -municode -O2 -Wl,--subsystem,windows
if errorlevel 1 exit /b 1
echo Built bin\mirefir.exe
exit /b 0
