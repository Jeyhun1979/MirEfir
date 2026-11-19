# Кладёт mpv (x86_64 без AVX) и vulkan-1.dll в vendor\mpv. Если mpv.exe уже там — ничего не делает.
# Источник: shinchiro/mpv-winbuild-cmake (обычная сборка, не v3/v4, т.е. без AVX2).
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$dest = Join-Path $PSScriptRoot 'vendor\mpv'
$tools = Join-Path $PSScriptRoot 'tools'
New-Item -ItemType Directory -Force $dest, $tools | Out-Null

if (-not (Test-Path (Join-Path $dest 'mpv.exe'))) {
  # 1) уже скачанный 1.0.24 плеер в профиле
  $old = Join-Path $env:APPDATA 'MirEfir\mpv-bin'
  $found = if (Test-Path $old) { Get-ChildItem $old -Recurse -Filter mpv.exe | Select-Object -First 1 }
  if ($found) {
    Write-Host 'mpv: копирую из профиля MirEfir\mpv-bin'
    Copy-Item (Join-Path $found.DirectoryName '*') $dest -Recurse -Force
  } else {
    Write-Host 'mpv: скачиваю последнюю сборку shinchiro'
    $rel = Invoke-RestMethod 'https://api.github.com/repos/shinchiro/mpv-winbuild-cmake/releases/latest' -Headers @{ 'User-Agent' = 'MirEfir-build' }
    $asset = $rel.assets | Where-Object { $_.name -match '^mpv-x86_64-\d' -and $_.name -like '*.7z' -and $_.name -notmatch '-v3-|-v4-|dev' } | Select-Object -First 1
    if (-not $asset) { throw 'Не найден пакет mpv-x86_64-*.7z' }
    $archive = Join-Path $tools $asset.name
    Invoke-WebRequest $asset.browser_download_url -OutFile $archive
    $sevenZr = Join-Path $tools '7zr.exe'
    if (-not (Test-Path $sevenZr)) { Invoke-WebRequest 'https://github.com/ip7z/7zip/releases/download/25.01/7zr.exe' -OutFile $sevenZr }
    & $sevenZr x $archive "-o$dest" -y | Out-Null
    Remove-Item $archive -Force
  }
}
if (-not (Test-Path (Join-Path $dest 'mpv.exe'))) { throw 'После подготовки нет vendor\mpv\mpv.exe' }

$vk = Join-Path $dest 'vulkan-1.dll'
if (-not (Test-Path $vk)) {
  $sys = Join-Path $env:SystemRoot 'System32\vulkan-1.dll'
  if (Test-Path $sys) { Copy-Item $sys $vk }
  else {
    Write-Host 'vulkan-1.dll: скачиваю загрузчик Vulkan'
    $nupkg = Join-Path $tools 'vulkan-loader.zip'
    Invoke-WebRequest 'https://api.nuget.org/v3-flatcontainer/silk.net.vulkan.loader.native/2025.9.12/silk.net.vulkan.loader.native.2025.9.12.nupkg' -OutFile $nupkg
    $tmp = Join-Path $tools 'vulkan-unpack'
    if (Test-Path $tmp) { Remove-Item $tmp -Recurse -Force }
    Expand-Archive $nupkg $tmp -Force
    $dll = Get-ChildItem $tmp -Recurse -Filter vulkan-1.dll | Where-Object { $_.FullName -match '\\win-x64\\' } | Select-Object -First 1
    if (-not $dll) { throw 'В пакете нет vulkan-1.dll' }
    Copy-Item $dll.FullName $vk
  }
}
Write-Host 'mpv готов: vendor\mpv'
