!macro KillMirEfir
  ExecWait '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Sleep 4000
  ExecWait '"$SYSDIR\taskkill.exe" /F /IM MirEfir.exe /T'
  Sleep 2000
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
