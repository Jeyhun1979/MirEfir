!macro KillMirEfir
  ExecWait '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Sleep 2500
  ExecWait '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Sleep 1500
!macroend

!macro preInit
  !insertmacro KillMirEfir
!macroend

!macro customInit
  !insertmacro KillMirEfir
!macroend

!macro customCheckAppRunning
  !insertmacro KillMirEfir
!macroend
