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
  ; Silent update: the player splash already started Setup and will relaunch.
  ; Do not spawn a second helper that can kill the new window.
  IfSilent mirefir_after_post
  IfFileExists "$INSTDIR\resources\post-launch.vbs" 0 mirefir_after_post
    CopyFiles /SILENT "$INSTDIR\resources\post-launch.vbs" "$TEMP\mirefir-post-launch.vbs"
    FileOpen $0 "$TEMP\mirefir-post-run.vbs" w
    FileWrite $0 'Set sh = CreateObject("WScript.Shell")$\r$\n'
    FileWrite $0 'sh.Run "wscript.exe //B //Nologo ""$TEMP\mirefir-post-launch.vbs"" ""$INSTDIR\MirEfir.exe"" ""$INSTDIR""", 0, False$\r$\n'
    FileClose $0
    ExecShell "open" "$SYSDIR\wscript.exe" '//B //Nologo "$TEMP\mirefir-post-run.vbs"' SW_HIDE
  mirefir_after_post:
!macroend
