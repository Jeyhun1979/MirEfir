param(
  [string]$Exe,
  [string]$Dir,
  [string]$Setup = '',
  [string]$Log = '',
  [string]$Lock = '',
  [switch]$RunSetup
)

$ErrorActionPreference = 'Continue'
if (-not $Exe) { $Exe = Join-Path ${env:ProgramFiles} 'MirEfir\MirEfir.exe' }
if (-not $Dir) { $Dir = Split-Path -Parent $Exe }
if (-not $Log) { $Log = Join-Path $env:APPDATA 'MirEfir\updater.log' }
if (-not $Lock) { $Lock = Join-Path $env:APPDATA 'MirEfir\installing.lock' }

$mutex = New-Object System.Threading.Mutex($false, 'MirEfirInstallSplash')
if (-not $mutex.WaitOne(0)) { exit 0 }

function Write-Log([string]$Message) {
  try {
    $folder = Split-Path -Parent $Log
    if ($folder -and -not (Test-Path $folder)) { New-Item -ItemType Directory -Path $folder -Force | Out-Null }
    $line = '{0} {1}' -f (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ss.fffZ'), $Message
    Add-Content -LiteralPath $Log -Value $line -Encoding UTF8
  } catch {}
}

function Pump {
  [System.Windows.Forms.Application]::DoEvents()
}

function Test-SetupRunning {
  $names = Get-Process -ErrorAction SilentlyContinue | ForEach-Object { $_.ProcessName }
  foreach ($name in $names) {
    if ($name -like 'MirEfir-Setup*' -or $name -eq 'Un_A') { return $true }
  }
  return $false
}

function Test-PlayerWindow {
  $list = @(Get-Process -Name 'MirEfir' -ErrorAction SilentlyContinue)
  foreach ($item in $list) {
    if ($item.MainWindowHandle -ne [IntPtr]::Zero) { return $true }
  }
  return $false
}

function Stop-Player {
  Get-Process -Name 'MirEfir' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
}

function Remove-LaunchTask {
  try {
    $svc = New-Object -ComObject Schedule.Service
    $svc.Connect()
    $svc.GetFolder('\').DeleteTask('MirEfirPostInstall', 0)
  } catch {}
}

function Start-Player {
  try {
    $svc = New-Object -ComObject Schedule.Service
    $svc.Connect()
    $folder = $svc.GetFolder('\')
    $task = $svc.NewTask(0)
    $task.Settings.StartWhenAvailable = $true
    $task.Settings.DisallowStartIfOnBatteries = $false
    $task.Settings.StopIfGoingOnBatteries = $false
    $task.Settings.AllowDemandStart = $true
    $task.Settings.MultipleInstances = 0
    $task.Settings.ExecutionTimeLimit = 'PT2M'
    $task.Principal.LogonType = 3
    $task.Principal.RunLevel = 0
    $action = $task.Actions.Create(0)
    $action.Path = $Exe
    $action.Arguments = '--updated'
    $action.WorkingDirectory = $Dir
    $folder.RegisterTaskDefinition('MirEfirPostInstall', $task, 6, $null, $null, 3) | Out-Null
    $folder.GetTask('MirEfirPostInstall').Run($null) | Out-Null
    Write-Log 'launch-scheduled-ok'
    return
  } catch {
    Write-Log ('launch-scheduled-error ' + $_.Exception.Message)
  }
  try {
    $line = '"{0}" --updated' -f $Exe
    $result = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
      CommandLine = $line
      CurrentDirectory = $Dir
    }
    Write-Log ('launch-wmi ' + $result.ReturnValue)
    if ([int]$result.ReturnValue -eq 0) { return }
  } catch {
    Write-Log ('launch-wmi-error ' + $_.Exception.Message)
  }
  $info = New-Object System.Diagnostics.ProcessStartInfo
  $info.FileName = $Exe
  $info.WorkingDirectory = $Dir
  $info.Arguments = '--updated'
  $info.UseShellExecute = $true
  [System.Diagnostics.Process]::Start($info) | Out-Null
  Write-Log 'launch-shellexecute'
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
try {
  Add-Type -Name Native -Namespace MirEfirSplash -MemberDefinition @"
    [System.Runtime.InteropServices.DllImport("kernel32.dll")] public static extern System.IntPtr GetConsoleWindow();
    [System.Runtime.InteropServices.DllImport("user32.dll")] public static extern bool ShowWindow(System.IntPtr hWnd, int nCmdShow);
"@
  [MirEfirSplash.Native]::ShowWindow([MirEfirSplash.Native]::GetConsoleWindow(), 0) | Out-Null
} catch {}

[System.Windows.Forms.Application]::EnableVisualStyles()

$form = New-Object System.Windows.Forms.Form
$form.Text = 'MirEfir'
$form.FormBorderStyle = [System.Windows.Forms.FormBorderStyle]::None
$form.StartPosition = [System.Windows.Forms.FormStartPosition]::CenterScreen
$form.Size = New-Object System.Drawing.Size(460, 200)
$form.BackColor = [System.Drawing.Color]::FromArgb(6, 7, 10)
$form.TopMost = $true
$form.ShowInTaskbar = $true
$form.ControlBox = $false

$title = New-Object System.Windows.Forms.Label
$title.Text = 'Установка'
$title.ForeColor = [System.Drawing.Color]::White
$title.Font = New-Object System.Drawing.Font('Segoe UI', 22, [System.Drawing.FontStyle]::Bold)
$title.AutoSize = $true
$title.Location = New-Object System.Drawing.Point(36, 38)
$form.Controls.Add($title)

$status = New-Object System.Windows.Forms.Label
$status.Text = 'Копируем файлы. Если Windows спросит про сеть — разрешите.'
$status.ForeColor = [System.Drawing.Color]::FromArgb(170, 176, 188)
$status.Font = New-Object System.Drawing.Font('Segoe UI', 10)
$status.Size = New-Object System.Drawing.Size(390, 40)
$status.Location = New-Object System.Drawing.Point(38, 82)
$form.Controls.Add($status)

$barBg = New-Object System.Windows.Forms.Panel
$barBg.BackColor = [System.Drawing.Color]::FromArgb(32, 36, 44)
$barBg.Size = New-Object System.Drawing.Size(384, 8)
$barBg.Location = New-Object System.Drawing.Point(38, 140)
$form.Controls.Add($barBg)

$bar = New-Object System.Windows.Forms.Panel
$bar.BackColor = [System.Drawing.Color]::FromArgb(56, 189, 248)
$bar.Size = New-Object System.Drawing.Size(40, 8)
$bar.Location = New-Object System.Drawing.Point(0, 0)
$barBg.Controls.Add($bar)

$tick = 0
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 40
$timer.Add_Tick({
  $script:tick = ($script:tick + 6) % 360
  $bar.Left = [Math]::Min($script:tick, 344)
})
$timer.Start()

$form.Show()
$form.Activate()
Pump
Write-Log 'splash-ready'

try {
  if ($RunSetup) {
    Write-Log 'killing-old'
    Stop-Player
    Start-Sleep -Seconds 2
    Pump
    if ($Setup -and (Test-Path -LiteralPath $Setup)) {
      Write-Log 'running-setup'
      $setupProc = Start-Process -FilePath $Setup -ArgumentList '/S','/NCRC' -PassThru
      while ($setupProc -and -not $setupProc.HasExited) {
        Pump
        Start-Sleep -Milliseconds 120
      }
      $code = 0
      if ($setupProc) { $code = $setupProc.ExitCode }
      Write-Log ("setup-exit " + $code)
    }
  } else {
    Write-Log 'waiting-setup'
    $waitUntil = (Get-Date).AddMinutes(8)
    while ((Get-Date) -lt $waitUntil) {
      Pump
      if (-not (Test-SetupRunning)) { break }
      Start-Sleep -Milliseconds 200
    }
    $hold = (Get-Date).AddSeconds(12)
    while ((Get-Date) -lt $hold) {
      Pump
      Start-Sleep -Milliseconds 200
    }
  }

  $status.Text = 'Обновление установлено, запускаем приложение'
  Pump
  Start-Sleep -Seconds 1
  Stop-Player
  Start-Sleep -Milliseconds 800
  Pump

  $ok = $false
  for ($try = 1; $try -le 6; $try++) {
    Write-Log ("launch-try " + $try)
    try { Start-Player } catch { Write-Log ("launch-error " + $_.Exception.Message) }
    $deadline = (Get-Date).AddSeconds(12)
    $seen = $false
    while ((Get-Date) -lt $deadline) {
      Pump
      Start-Sleep -Milliseconds 250
      if (Test-PlayerWindow) {
        $seen = $true
        break
      }
    }
    if ($seen) {
      $stableUntil = (Get-Date).AddSeconds(8)
      while ((Get-Date) -lt $stableUntil) {
        Pump
        Start-Sleep -Milliseconds 250
      }
      if (Test-PlayerWindow) {
        $ok = $true
        break
      }
      Write-Log 'launch-died-after-window'
    } else {
      Write-Log ("launch-died " + $try)
    }
    Stop-Player
    Start-Sleep -Milliseconds 600
  }

  if ($ok) {
    Write-Log 'relaunched'
  } else {
    Write-Log 'launch-failed'
    $status.Text = 'Не удалось открыть плеер. Запустите его с ярлыка.'
    Pump
    $holdFail = (Get-Date).AddSeconds(10)
    while ((Get-Date) -lt $holdFail) {
      Pump
      Start-Sleep -Milliseconds 200
    }
  }
} finally {
  $timer.Stop()
  Remove-LaunchTask
  try { if ($Setup -and (Test-Path -LiteralPath $Setup)) { Remove-Item -LiteralPath $Setup -Force -ErrorAction SilentlyContinue } } catch {}
  try { if ($Lock -and (Test-Path -LiteralPath $Lock)) { Remove-Item -LiteralPath $Lock -Force -ErrorAction SilentlyContinue } } catch {}
  Write-Log 'splash-end'
  try { $form.Close() } catch {}
  try { $mutex.ReleaseMutex() } catch {}
}
