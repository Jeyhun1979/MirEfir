Option Explicit
Dim exe, dir, i, svc, folder, task, action

If WScript.Arguments.Count < 1 Then WScript.Quit 1
exe = WScript.Arguments(0)
If WScript.Arguments.Count >= 2 Then
  dir = WScript.Arguments(1)
Else
  dir = ""
End If

Function PlayerRunning()
  Dim wmi, procs
  Set wmi = GetObject("winmgmts:\\.\root\cimv2")
  Set procs = wmi.ExecQuery("SELECT ProcessId FROM Win32_Process WHERE Name='MirEfir.exe'")
  PlayerRunning = (procs.Count > 0)
End Function

Sub LaunchDetached()
  On Error Resume Next
  Set svc = CreateObject("Schedule.Service")
  svc.Connect
  Set folder = svc.GetFolder("\")
  folder.DeleteTask "MirEfirPostInstall", 0
  Err.Clear
  Set task = svc.NewTask(0)
  task.Settings.StartWhenAvailable = True
  task.Settings.DisallowStartIfOnBatteries = False
  task.Settings.StopIfGoingOnBatteries = False
  task.Settings.AllowDemandStart = True
  task.Settings.MultipleInstances = 0
  task.Settings.ExecutionTimeLimit = "PT2M"
  task.Principal.LogonType = 3
  task.Principal.RunLevel = 0
  Set action = task.Actions.Create(0)
  action.Path = exe
  action.Arguments = "--updated"
  If dir <> "" Then action.WorkingDirectory = dir
  folder.RegisterTaskDefinition "MirEfirPostInstall", task, 6, Null, Null, 3
  folder.GetTask("MirEfirPostInstall").Run Null
  Err.Clear
End Sub

Sub ForgetTask()
  On Error Resume Next
  Set svc = CreateObject("Schedule.Service")
  svc.Connect
  svc.GetFolder("\").DeleteTask "MirEfirPostInstall", 0
End Sub

WScript.Sleep 6000
For i = 1 To 24
  If PlayerRunning() Then
    WScript.Sleep 8000
    If PlayerRunning() Then
      ForgetTask
      WScript.Quit 0
    End If
  End If
  LaunchDetached
  WScript.Sleep 4000
Next
ForgetTask
