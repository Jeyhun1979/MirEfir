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
