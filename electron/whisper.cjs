const { app, BrowserWindow, net } = require('electron')
const fs = require('fs')
const os = require('os')
const path = require('path')
const { spawn } = require('child_process')

const WHISPER_VERSION = '1.9.2'
const MODEL_NAME = 'ggml-base-q5_1.bin'
const MODEL_URL = `https://huggingface.co/ggerganov/whisper.cpp/resolve/main/${MODEL_NAME}`
const MIN_MODEL_BYTES = 40 * 1024 * 1024

let pending = null
let currentChild = null

function sendProgress(payload) {
  const win = BrowserWindow.getAllWindows()[0]
  if (win && !win.isDestroyed()) win.webContents.send('speech:progress', payload)
}

function whisperRoot() {
  return path.join(app.getPath('userData'), 'whisper')
}

function modelPath() {
  return path.join(whisperRoot(), MODEL_NAME)
}

function binDir() {
  return path.join(whisperRoot(), 'bin')
}

function binArchive() {
  const ext = process.platform === 'win32' ? 'zip' : 'tar.gz'
  return path.join(whisperRoot(), `whisper-${WHISPER_VERSION}.${ext}`)
}

function binUrl() {
  const base = `https://github.com/ggml-org/whisper.cpp/releases/download/v${WHISPER_VERSION}`
  if (process.platform === 'win32') {
    return process.arch === 'ia32' ? `${base}/whisper-bin-Win32.zip` : `${base}/whisper-bin-x64.zip`
  }
  if (process.arch === 'arm64') return `${base}/whisper-bin-ubuntu-arm64.tar.gz`
  return `${base}/whisper-bin-ubuntu-x64.tar.gz`
}

function findCli(root = binDir()) {
  const want = process.platform === 'win32' ? ['whisper-cli.exe', 'main.exe'] : ['whisper-cli', 'main']
  const stack = [root]
  while (stack.length) {
    const dir = stack.pop()
    let entries = []
    try {
      entries = fs.readdirSync(dir, { withFileTypes: true })
    } catch {
      continue
    }
    for (const entry of entries) {
      const full = path.join(dir, entry.name)
      if (entry.isDirectory()) stack.push(full)
      else if (want.includes(entry.name)) return full
    }
  }
  return ''
}

function modelReady() {
  try {
    return fs.existsSync(modelPath()) && fs.statSync(modelPath()).size >= MIN_MODEL_BYTES
  } catch {
    return false
  }
}

function engineReady() {
  return Boolean(findCli())
}

function runTar(args, cwd) {
  return new Promise((resolve, reject) => {
    const child = spawn('tar', args, { cwd, windowsHide: true })
    let err = ''
    child.stderr.on('data', (chunk) => {
      err += chunk.toString()
    })
    child.on('error', reject)
    child.on('close', (code) => {
      if (code === 0) resolve()
      else reject(new Error(err.trim() || 'tar failed'))
    })
  })
}

function downloadFile(url, dest, onBytes) {
  return new Promise((resolve, reject) => {
    fs.mkdirSync(path.dirname(dest), { recursive: true })
    const request = net.request({ url, redirect: 'follow' })
    request.setHeader('User-Agent', 'MirEfir')
    request.on('response', (response) => {
      if (response.statusCode >= 400) {
        reject(new Error(`Не удалось скачать голосовой модуль (${response.statusCode})`))
        return
      }
      const file = fs.createWriteStream(dest)
      const total = Number(response.headers['content-length'] || 0)
      let received = 0
      response.on('data', (chunk) => {
        file.write(chunk)
        received += chunk.length
        onBytes?.(received, total)
      })
      response.on('end', () => {
        file.end()
        file.on('finish', () => resolve(dest))
      })
      response.on('error', (err) => {
        file.destroy()
        reject(err)
      })
    })
    request.on('error', reject)
    request.end()
  })
}

function toPcmBuffer(raw) {
  if (!raw) return Buffer.alloc(0)
  if (Buffer.isBuffer(raw)) return raw
  if (raw instanceof ArrayBuffer) return Buffer.from(raw)
  if (ArrayBuffer.isView(raw)) return Buffer.from(raw.buffer, raw.byteOffset, raw.byteLength)
  if (raw.type === 'Buffer' && Array.isArray(raw.data)) return Buffer.from(raw.data)
  return Buffer.from(raw)
}

function writeWav(file, pcm, sampleRate) {
  const header = Buffer.alloc(44)
  header.write('RIFF', 0)
  header.writeUInt32LE(36 + pcm.length, 4)
  header.write('WAVE', 8)
  header.write('fmt ', 12)
  header.writeUInt32LE(16, 16)
  header.writeUInt16LE(1, 20)
  header.writeUInt16LE(1, 22)
  header.writeUInt32LE(sampleRate, 24)
  header.writeUInt32LE(sampleRate * 2, 28)
  header.writeUInt16LE(2, 32)
  header.writeUInt16LE(16, 34)
  header.write('data', 36)
  header.writeUInt32LE(pcm.length, 40)
  fs.writeFileSync(file, Buffer.concat([header, pcm]))
}

function pruneVosk() {
  try {
    fs.rmSync(path.join(app.getPath('userData'), 'vosk'), { recursive: true, force: true })
  } catch {
    /* old model may be locked */
  }
}

async function ensureModel() {
  if (modelReady()) return
  fs.mkdirSync(whisperRoot(), { recursive: true })
  sendProgress({ phase: 'download', text: 'Скачиваю голосовую модель…', received: 0, total: 0 })
  const tmp = `${modelPath()}.part`
  await downloadFile(MODEL_URL, tmp, (received, total) => {
    sendProgress({ phase: 'download', text: 'Скачиваю голосовую модель…', received, total })
  })
  if (!fs.existsSync(tmp) || fs.statSync(tmp).size < MIN_MODEL_BYTES) {
    throw new Error('Модель голоса скачалась повреждённой')
  }
  fs.renameSync(tmp, modelPath())
}

async function ensureEngine() {
  if (engineReady()) return
  fs.mkdirSync(binDir(), { recursive: true })
  sendProgress({ phase: 'download', text: 'Скачиваю движок распознавания…', received: 0, total: 0 })
  const archive = binArchive()
  await downloadFile(binUrl(), archive, (received, total) => {
    sendProgress({ phase: 'download', text: 'Скачиваю движок распознавания…', received, total })
  })
  sendProgress({ phase: 'extract', text: 'Распаковываю движок распознавания…' })
  if (archive.endsWith('.zip')) await runTar(['-xf', archive, '-C', binDir()])
  else await runTar(['-xzf', archive, '-C', binDir()])
  try {
    fs.unlinkSync(archive)
  } catch {
    /* keep archive if locked */
  }
  if (!engineReady()) throw new Error('Не удалось подготовить движок распознавания')
}

async function ensureWhisper() {
  if (pending) return pending
  pending = (async () => {
    try {
      await ensureEngine()
      await ensureModel()
      pruneVosk()
      sendProgress({ phase: 'ready', text: '' })
      return { ok: true, native: true }
    } catch (err) {
      return { ok: false, error: err.message || 'Не удалось подготовить голосовую модель.' }
    } finally {
      pending = null
    }
  })()
  return pending
}

function cancelWhisper() {
  if (!currentChild?.pid) {
    currentChild = null
    return
  }
  const child = currentChild
  currentChild = null
  try {
    if (process.platform === 'win32') spawn('taskkill', ['/PID', String(child.pid), '/T', '/F'], { windowsHide: true })
    else child.kill('SIGKILL')
  } catch {
    /* already gone */
  }
}

function runCli(args, cwd) {
  return new Promise((resolve, reject) => {
    const cli = findCli()
    if (!cli) {
      reject(new Error('Движок распознавания не найден'))
      return
    }
    const child = spawn(cli, args, { cwd, windowsHide: true })
    currentChild = child
    let out = ''
    let err = ''
    child.stdout.on('data', (chunk) => {
      out += chunk.toString()
    })
    child.stderr.on('data', (chunk) => {
      err += chunk.toString()
    })
    child.on('error', reject)
    child.on('close', (code) => {
      if (currentChild === child) currentChild = null
      if (code === 0 || out || fs.existsSync(path.join(cwd, 'out.json'))) resolve(out)
      else reject(new Error(err.trim() || `whisper failed (${code})`))
    })
  })
}

function textFromJson(file) {
  try {
    const data = JSON.parse(fs.readFileSync(file, 'utf8'))
    if (typeof data.text === 'string' && data.text.trim()) return data.text.trim()
    const parts = Array.isArray(data.transcription) ? data.transcription : []
    return parts
      .map((item) => String(item?.text || '').trim())
      .filter(Boolean)
      .join(' ')
      .trim()
  } catch {
    return ''
  }
}

function textFromStdout(raw) {
  return String(raw || '')
    .split(/\r?\n/)
    .map((line) => line.trim())
    .filter((line) => line && !/^\[/.test(line) && !/whisper|ggml|system_info|main:/i.test(line))
    .join(' ')
    .replace(/\s+/g, ' ')
    .trim()
}

async function transcribePcm(payload = {}) {
  const pcm = toPcmBuffer(payload.pcm || payload)
  if (pcm.length < 3200) return { ok: false, error: 'NO_AUDIO' }
  const ready = await ensureWhisper()
  if (!ready?.ok) return { ok: false, error: ready?.error || 'NO_WHISPER' }
  const sampleRate = Number(payload.sampleRate) || 16000
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'mirefir-whisper-'))
  const wav = path.join(dir, 'speech.wav')
  const outBase = path.join(dir, 'out')
  writeWav(wav, pcm, sampleRate)
  const threads = Math.max(1, Math.min(4, os.cpus()?.length || 2))
  try {
    const stdout = await runCli(
      ['-m', modelPath(), '-f', wav, '-l', 'auto', '-nt', '-np', '-t', String(threads), '-oj', '-of', outBase],
      dir,
    )
    const text = textFromJson(`${outBase}.json`) || textFromStdout(stdout)
    if (!text) return { ok: false, error: 'Не услышали. Скажите название канала или «переключи на …».' }
    return { ok: true, text }
  } catch (err) {
    return { ok: false, error: err.message || 'Не удалось распознать голос.' }
  } finally {
    try {
      fs.rmSync(dir, { recursive: true, force: true })
    } catch {
      /* tmp leftovers */
    }
  }
}

module.exports = { ensureWhisper, transcribePcm, cancelWhisper, modelReady }
