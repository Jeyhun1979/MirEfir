Option Explicit
Dim exe, dir, i, sh, wmi, procs, p, name, running

If WScript.Arguments.Count < 1 Then WScript.Quit 1
exe = WScript.Arguments(0)
If WScript.Arguments.Count >= 2 Then
  dir = WScript.Arguments(1)
Else
  dir = ""
End If

Function PlayerRunning()
  Dim list
  Set wmi = GetObject("winmgmts:\\.\root\cimv2")
  Set list = wmi.ExecQuery("SELECT ProcessId FROM Win32_Process WHERE Name='MirEfir.exe'")
  PlayerRunning = (list.Count > 0)
End Function

Function SetupRunning()
  SetupRunning = False
  Set wmi = GetObject("winmgmts:\\.\root\cimv2")
  Set procs = wmi.ExecQuery("SELECT Name FROM Win32_Process")
  For Each p In procs
    name = LCase(p.Name)
    If Left(name, 13) = "mirefir-setup" Or name = "un_a.exe" Then
      SetupRunning = True
      Exit Function
    End If
  Next
End Function

Sub LaunchPlayer()
  On Error Resume Next
  Set sh = CreateObject("WScript.Shell")
  If dir <> "" Then sh.CurrentDirectory = dir
  sh.Run "cmd.exe /c start """" /D """ & dir & """ """ & exe & """ --updated", 0, False
End Sub

i = 0
Do While SetupRunning() And i < 360
  WScript.Sleep 2000
  i = i + 1
Loop
WScript.Sleep 2000

For i = 1 To 8
  If PlayerRunning() Then
    WScript.Sleep 10000
    If PlayerRunning() Then WScript.Quit 0
  End If
  LaunchPlayer
  WScript.Sleep 4000
Next
