; Silent in-app updates from 1.0.6 run Setup /S, then the old uninstaller
; may abort with exit code 2 while a file is still locked. Do not show a
; console for taskkill, retry deletion, and keep installing anyway.

!macro KillMirEfirHidden
  nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Pop $0
  Sleep 1500
  nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Pop $0
  Sleep 2000
!macroend

!macro customInit
  !insertmacro KillMirEfirHidden
!macroend

!macro customCheckAppRunning
  !insertmacro KillMirEfirHidden
!macroend

!macro customRemoveFiles
  SetOutPath "$TEMP"
  StrCpy $R8 0
  mirefir_remove_retry:
    IntOp $R8 $R8 + 1
    nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
    Pop $0
    Sleep 1500
    ClearErrors
    RMDir /r "$INSTDIR"
    IfFileExists "$INSTDIR\MirEfir.exe" 0 mirefir_remove_done
    IntCmp $R8 15 mirefir_remove_done mirefir_remove_retry mirefir_remove_done
  mirefir_remove_done:
    ClearErrors
!macroend

!macro customUnInstallCheck
  ${If} $R0 == 0
    Goto mirefir_uncheck_done
  ${EndIf}

  DetailPrint "Previous uninstall returned $R0, retrying file removal"
  StrCpy $R8 0
  mirefir_uncheck_retry:
    IntOp $R8 $R8 + 1
    nsExec::Exec '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
    Pop $0
    Sleep 2000
    RMDir /r "$INSTDIR"
    IfFileExists "$INSTDIR\MirEfir.exe" 0 mirefir_uncheck_ok
    IntCmp $R8 12 mirefir_uncheck_ok mirefir_uncheck_retry mirefir_uncheck_ok
  mirefir_uncheck_ok:
    StrCpy $R0 0
    ClearErrors
  mirefir_uncheck_done:
!macroend
