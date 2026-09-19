; Skip the old full uninstall (it deletes the whole folder and takes minutes).
; Kill the running app quietly, then overwrite files in place.

!macro KillMirEfirHidden
  nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Pop $0
  Sleep 400
!macroend

!macro customInit
  !insertmacro KillMirEfirHidden
  ClearErrors
  DeleteRegValue SHELL_CONTEXT "${UNINSTALL_REGISTRY_KEY}" UninstallString
  ClearErrors
  DeleteRegValue HKLM "${UNINSTALL_REGISTRY_KEY}" UninstallString
  ClearErrors
  DeleteRegValue HKCU "${UNINSTALL_REGISTRY_KEY}" UninstallString
  ClearErrors
!macroend

!macro customCheckAppRunning
  !insertmacro KillMirEfirHidden
!macroend

!macro customRemoveFiles
  SetOutPath "$TEMP"
  nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Pop $0
  Sleep 400
  ClearErrors
  RMDir /r "$INSTDIR"
  ClearErrors
!macroend

!macro customUnInstallCheck
  StrCpy $R0 0
  ClearErrors
!macroend

!macro customInstall
  nsExec::ExecToLog '"$SYSDIR\netsh.exe" advfirewall firewall delete rule name="MirEfir"'
  Pop $0
  nsExec::ExecToLog '"$SYSDIR\netsh.exe" advfirewall firewall add rule name="MirEfir" dir=in action=allow program="$INSTDIR\MirEfir.exe" enable=yes profile=any'
  Pop $0
  nsExec::ExecToLog '"$SYSDIR\netsh.exe" advfirewall firewall add rule name="MirEfir Out" dir=out action=allow program="$INSTDIR\MirEfir.exe" enable=yes profile=any'
  Pop $0
  IfFileExists "$INSTDIR\resources\install-splash.ps1" 0 mirefir_after_splash
    CopyFiles /SILENT "$INSTDIR\resources\install-splash.ps1" "$TEMP\install-splash.ps1"
    FileOpen $0 "$TEMP\mirefir-nsis-splash.vbs" w
    FileWrite $0 'Set sh = CreateObject("WScript.Shell")$\r$\n'
    FileWrite $0 'sh.Run "powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File ""'
    FileWrite $0 "$TEMP\install-splash.ps1"
    FileWrite $0 '"" -Exe ""'
    FileWrite $0 "$INSTDIR\MirEfir.exe"
    FileWrite $0 '"" -Dir ""'
    FileWrite $0 "$INSTDIR"
    FileWrite $0 '"" -Log ""'
    FileWrite $0 "$APPDATA\MirEfir\updater.log"
    FileWrite $0 '"" -Lock ""'
    FileWrite $0 "$APPDATA\MirEfir\installing.lock"
    FileWrite $0 '"", 0, False$\r$\n'
    FileClose $0
    ExecShell "open" "$SYSDIR\wscript.exe" '//B //Nologo "$TEMP\mirefir-nsis-splash.vbs"' SW_HIDE
  mirefir_after_splash:
  IfFileExists "$INSTDIR\resources\post-launch.vbs" 0 mirefir_after_post
    CopyFiles /SILENT "$INSTDIR\resources\post-launch.vbs" "$TEMP\mirefir-post-launch.vbs"
    FileOpen $0 "$TEMP\mirefir-post-run.vbs" w
    FileWrite $0 'Set sh = CreateObject("WScript.Shell")$\r$\n'
    FileWrite $0 'sh.Run "wscript.exe //B //Nologo ""$TEMP\mirefir-post-launch.vbs"" ""$INSTDIR\MirEfir.exe"" ""$INSTDIR""", 0, False$\r$\n'
    FileClose $0
    ExecShell "open" "$SYSDIR\wscript.exe" '//B //Nologo "$TEMP\mirefir-post-run.vbs"' SW_HIDE
  mirefir_after_post:
!macroend
