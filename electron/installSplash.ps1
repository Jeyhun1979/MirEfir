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
    $fs = [IO.File]::Open($Log, 'Append', 'Write', 'ReadWrite')
    try {
      $bytes = [Text.Encoding]::UTF8.GetBytes($line + [Environment]::NewLine)
      $fs.Write($bytes, 0, $bytes.Length)
    } finally { $fs.Close() }
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

function Unblock-Setup {
  if (-not $Setup -or -not (Test-Path -LiteralPath $Setup)) { return }
  try { Unblock-File -LiteralPath $Setup -ErrorAction SilentlyContinue } catch {}
  try { [IO.File]::Delete("$Setup`:Zone.Identifier") } catch {}
}

function Start-SetupViaCmd {
  if (-not $RunSetup) { return }
  if (-not $Setup -or -not (Test-Path -LiteralPath $Setup)) {
    Write-Log 'setup-cmd-missing'
    return
  }
  Unblock-Setup
  $runner = Join-Path $env:TEMP 'mirefir-run-setup.cmd'
  $logQ = [string]$Log -replace '"', ''
  $setupQ = [string]$Setup -replace '"', ''
  $dirQ = [string]$Dir -replace '"', ''
  $exeQ = [string]$Exe -replace '"', ''
  $lockQ = [string]$Lock -replace '"', ''
  $lines = @(
    '@echo off',
    'setlocal EnableExtensions',
    ('echo apply-start %date% %time%>>"{0}"' -f $logQ),
    ('echo running-setup>>"{0}"' -f $logQ),
    ('start /wait "" "{0}" /S /NCRC' -f $setupQ),
    ('echo setup-exit %ERRORLEVEL%>>"{0}"' -f $logQ),
    ('"%SystemRoot%\System32\taskkill.exe" /F /IM MirEfir.exe >>"{0}" 2>&1' -f $logQ),
    'ping 127.0.0.1 -n 3 >nul',
    ('echo launch-try 1>>"{0}"' -f $logQ),
    ('start "" /D "{0}" "{1}" --updated' -f $dirQ, $exeQ),
    ('del /f /q "{0}" >nul 2>&1' -f $lockQ)
  )
  [IO.File]::WriteAllText($runner, ($lines -join "`r`n") + "`r`n")
  $cmdExe = Join-Path $env:SystemRoot 'System32\cmd.exe'
  Start-Process -FilePath $cmdExe -ArgumentList @('/c', $runner) -WindowStyle Hidden | Out-Null
  $script:setupCmdStarted = $true
  Write-Log 'setup-cmd-detached'
}

function Start-SetupNow {
  if ($script:setupCmdStarted) { return $true }
  if (-not $Setup -or -not (Test-Path -LiteralPath $Setup)) { return $false }
  Unblock-Setup
  Write-Log 'running-setup'
  try {
    $wsh = New-Object -ComObject WScript.Shell
    $wsh.Run(('"{0}" /S /NCRC' -f $Setup), 0, $false) | Out-Null
    Write-Log 'setup-wscript-ok'
    return $true
  } catch {
    Write-Log ('setup-wscript-error ' + $_.Exception.Message)
  }
  try {
    Start-Process -FilePath $Setup -ArgumentList '/S','/NCRC' -WindowStyle Hidden | Out-Null
    Write-Log 'setup-start-ok'
    return $true
  } catch {
    Write-Log ('setup-start-error ' + $_.Exception.Message)
    return $false
  }
}

function Wait-SetupFinished {
  $appearUntil = (Get-Date).AddSeconds(90)
  $seen = $false
  while ((Get-Date) -lt $appearUntil) {
    Pump
    if (Test-SetupRunning) {
      $seen = $true
      break
    }
    Start-Sleep -Milliseconds 200
  }
  if (-not $seen -and $RunSetup) {
    Start-SetupNow | Out-Null
    $appearUntil = (Get-Date).AddSeconds(60)
    while ((Get-Date) -lt $appearUntil) {
      Pump
      if (Test-SetupRunning) {
        $seen = $true
        break
      }
      Start-Sleep -Milliseconds 200
    }
  }
  if (-not $seen) {
    Write-Log 'setup-never-appeared'
    return
  }
  Write-Log 'setup-seen'
  $waitUntil = (Get-Date).AddMinutes(12)
  while ((Get-Date) -lt $waitUntil) {
    Pump
    if (-not (Test-SetupRunning)) { break }
    Start-Sleep -Milliseconds 200
  }
  Write-Log 'setup-exit'
  $hold = (Get-Date).AddSeconds(2)
  while ((Get-Date) -lt $hold) {
    Pump
    Start-Sleep -Milliseconds 200
  }
}

function Start-Player {
  $dirArg = $Dir.TrimEnd('\')
  $line = 'cmd.exe /c start "" /D "{0}" "{1}" --updated' -f $dirArg, $Exe
  try {
    $result = Invoke-CimMethod -ClassName Win32_Process -MethodName Create -Arguments @{
      CommandLine = $line
      CurrentDirectory = $dirArg
    }
    Write-Log ('launch-wmi ' + $result.ReturnValue)
    if ([int]$result.ReturnValue -eq 0) { return }
  } catch {
    Write-Log ('launch-wmi-error ' + $_.Exception.Message)
  }
  try {
    $wsh = New-Object -ComObject WScript.Shell
    $wsh.CurrentDirectory = $dirArg
    $wsh.Run($line, 0, $false) | Out-Null
    Write-Log 'launch-wscript-ok'
  } catch {
    Write-Log ('launch-wscript-error ' + $_.Exception.Message)
  }
}

Start-SetupViaCmd

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
  Wait-SetupFinished

  $status.Text = 'Обновление установлено, запускаем приложение'
  Pump

  $ok = $false
  for ($try = 1; $try -le 6; $try++) {
    Write-Log ("launch-try " + $try)
    if (-not (Test-PlayerWindow)) {
      try { Start-Player } catch { Write-Log ("launch-error " + $_.Exception.Message) }
    }
    $deadline = (Get-Date).AddSeconds(20)
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
  try { if ($Lock -and (Test-Path -LiteralPath $Lock)) { Remove-Item -LiteralPath $Lock -Force -ErrorAction SilentlyContinue } } catch {}
  Write-Log 'splash-end'
  try { $form.Close() } catch {}
  try { $mutex.ReleaseMutex() } catch {}
}
