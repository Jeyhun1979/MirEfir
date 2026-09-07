!macro customInit
  ExecWait 'taskkill /F /IM MirEfir.exe /T'
  Sleep 2000
!macroend

!macro customCheckAppRunning
  ExecWait 'taskkill /F /IM MirEfir.exe /T'
  Sleep 2000
!macroend
