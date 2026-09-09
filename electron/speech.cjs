const fs = require('fs')
const os = require('os')
const path = require('path')
const { spawn } = require('child_process')

let currentChild = null

function cancelWindowsSpeech() {
  if (!currentChild?.pid) {
    currentChild = null
    return
  }
  const pid = currentChild.pid
  currentChild = null
  try {
    spawn('taskkill', ['/PID', String(pid), '/T', '/F'], { windowsHide: true })
  } catch {
    /* already gone */
  }
}

function decodePayload(line) {
  const json = Buffer.from(line.slice(3), 'base64').toString('utf8')
  const data = JSON.parse(json)
  return {
    ok: true,
    text: String(data.text || '').trim(),
    intent: data.grammar === 'switch' ? 'switch' : data.grammar === 'search' ? 'search' : '',
    grammar: String(data.grammar || ''),
    confidence: Number(data.confidence) || 0,
  }
}

function listenWindowsSpeech(payload = {}) {
  if (process.platform !== 'win32') {
    return Promise.resolve({ ok: false, error: 'NO_PS' })
  }
  cancelWindowsSpeech()

  const wait = Math.max(5, Math.min(12, Number(payload.seconds) || 8))
  const phrases = Array.isArray(payload.phrases)
    ? payload.phrases.map((item) => String(item || '').trim()).filter((item) => item.length >= 2 && item.length <= 72)
    : []

  const dir = os.tmpdir()
  const script = path.join(dir, 'mirefir-speech.ps1')
  const namesPath = path.join(dir, 'mirefir-speech-names.json')
  fs.writeFileSync(namesPath, JSON.stringify(phrases), 'utf8')
  fs.writeFileSync(script, SPEECH_PS1, 'utf8')

  return new Promise((resolve) => {
    const child = spawn(
      'powershell.exe',
      ['-NoProfile', '-STA', '-ExecutionPolicy', 'Bypass', '-File', script, namesPath, String(wait)],
      { windowsHide: true },
    )
    currentChild = child
    let out = ''
    child.stdout.on('data', (chunk) => {
      out += chunk.toString('utf8')
    })
    child.on('error', () => {
      if (currentChild === child) currentChild = null
      resolve({ ok: false, error: 'NO_PS' })
    })
    child.on('close', () => {
      if (currentChild === child) currentChild = null
      const lines = String(out || '')
        .split(/\r?\n/)
        .map((line) => line.trim())
        .filter(Boolean)
      if (lines.includes('ERROR:NO_MIC')) {
        resolve({ ok: false, error: 'NO_MIC' })
        return
      }
      if (lines.includes('ERROR:NO_LANG')) {
        resolve({ ok: false, error: 'NO_LANG' })
        return
      }
      const okLine = [...lines].reverse().find((line) => line.startsWith('OK:')) || ''
      if (!okLine) {
        resolve({ ok: true, text: '', intent: '', grammar: '', confidence: 0 })
        return
      }
      try {
        resolve(decodePayload(okLine))
      } catch {
        resolve({ ok: false, error: 'DECODE' })
      }
    })
  })
}

const SPEECH_PS1 = [
  'Add-Type -AssemblyName System.Speech',
  '$namesPath = $args[0]',
  '$wait = 8',
  'if ($args.Count -ge 2) { $wait = [int]$args[1] }',
  'if ($wait -lt 5) { $wait = 5 }',
  '$phrases = @()',
  'if (Test-Path -LiteralPath $namesPath) {',
  '  try { $phrases = @(Get-Content -LiteralPath $namesPath -Raw -Encoding UTF8 | ConvertFrom-Json) } catch { $phrases = @() }',
  '}',
  '$info = $null',
  '$installed = [System.Speech.Recognition.SpeechRecognitionEngine]::InstalledRecognizers()',
  'foreach ($item in $installed) { if ($item.Culture.Name -like "ru*") { $info = $item; break } }',
  'if ($null -eq $info -and $installed.Count -gt 0) { $info = $installed[0] }',
  '$eng = $null',
  'if ($null -ne $info) { try { $eng = New-Object System.Speech.Recognition.SpeechRecognitionEngine($info) } catch {} }',
  'if ($null -eq $eng) { try { $eng = New-Object System.Speech.Recognition.SpeechRecognitionEngine } catch {} }',
  'if ($null -eq $eng) { Write-Output "ERROR:NO_LANG"; exit 0 }',
  'try { $eng.SetInputToDefaultAudioDevice() } catch { Write-Output "ERROR:NO_MIC"; exit 0 }',
  '$loaded = 0',
  'if ($phrases.Count -gt 0) {',
  '  $channelChoices = New-Object System.Speech.Recognition.Choices',
  '  foreach ($phrase in $phrases) {',
  '    $text = [string]$phrase',
  '    if ([string]::IsNullOrWhiteSpace($text)) { continue }',
  '    try { [void]$channelChoices.Add($text.Trim()) } catch {}',
  '  }',
  '  try {',
  '    $searchBuilder = New-Object System.Speech.Recognition.GrammarBuilder',
  '    if ($null -ne $eng.RecognizerInfo) { $searchBuilder.Culture = $eng.RecognizerInfo.Culture }',
  '    $searchBuilder.Append($channelChoices)',
  '    $searchGrammar = New-Object System.Speech.Recognition.Grammar($searchBuilder)',
  '    $searchGrammar.Name = "search"',
  '    $eng.LoadGrammar($searchGrammar)',
  '    $loaded++',
  '  } catch {}',
  '  try {',
  '    $prefix = New-Object System.Speech.Recognition.Choices',
  '    foreach ($item in @("переключи на","переключить на","включи","включить","открой","поставь","давай")) {',
  '      try { [void]$prefix.Add($item) } catch {}',
  '    }',
  '    $switchBuilder = New-Object System.Speech.Recognition.GrammarBuilder',
  '    if ($null -ne $eng.RecognizerInfo) { $switchBuilder.Culture = $eng.RecognizerInfo.Culture }',
  '    $switchBuilder.Append($prefix)',
  '    $switchBuilder.Append($channelChoices)',
  '    $switchGrammar = New-Object System.Speech.Recognition.Grammar($switchBuilder)',
  '    $switchGrammar.Name = "switch"',
  '    $eng.LoadGrammar($switchGrammar)',
  '    $loaded++',
  '  } catch {}',
  '  try {',
  '    $find = New-Object System.Speech.Recognition.Choices',
  '    foreach ($item in @("найди","найти","поиск","ищи")) { try { [void]$find.Add($item) } catch {} }',
  '    $findBuilder = New-Object System.Speech.Recognition.GrammarBuilder',
  '    if ($null -ne $eng.RecognizerInfo) { $findBuilder.Culture = $eng.RecognizerInfo.Culture }',
  '    $findBuilder.Append($find)',
  '    $findBuilder.Append($channelChoices)',
  '    $findGrammar = New-Object System.Speech.Recognition.Grammar($findBuilder)',
  '    $findGrammar.Name = "search"',
  '    $eng.LoadGrammar($findGrammar)',
  '    $loaded++',
  '  } catch {}',
  '}',
  'if ($loaded -lt 1) {',
  '  try {',
  '    $dictation = New-Object System.Speech.Recognition.DictationGrammar',
  '    $dictation.Name = "dictation"',
  '    $eng.LoadGrammar($dictation)',
  '    $loaded++',
  '  } catch { Write-Output "ERROR:NO_LANG"; $eng.Dispose(); exit 0 }',
  '}',
  '$eng.InitialSilenceTimeout = New-TimeSpan -Seconds 4',
  '$eng.BabbleTimeout = New-TimeSpan -Seconds 4',
  '$eng.EndSilenceTimeout = New-TimeSpan -Seconds 1.1',
  '$result = $eng.Recognize((New-TimeSpan -Seconds $wait))',
  '$eng.Dispose()',
  'if ($null -eq $result -or [string]::IsNullOrWhiteSpace($result.Text)) { Write-Output ""; exit 0 }',
  '$grammarName = ""',
  'if ($null -ne $result.Grammar) { $grammarName = [string]$result.Grammar.Name }',
  '$payload = @{ text = $result.Text.Trim(); grammar = $grammarName; confidence = [double]$result.Confidence }',
  '$json = ($payload | ConvertTo-Json -Compress)',
  '$bytes = [Text.Encoding]::UTF8.GetBytes($json)',
  'Write-Output ("OK:" + [Convert]::ToBase64String($bytes))',
].join('\r\n')

module.exports = { listenWindowsSpeech, cancelWindowsSpeech }
