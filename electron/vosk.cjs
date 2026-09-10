const { app, BrowserWindow, net } = require('electron')
const fs = require('fs')
const path = require('path')
const { spawn } = require('child_process')
const { pathToFileURL } = require('url')

const MODEL_DIR_NAME = 'vosk-model-small-ru-0.22'
const MODEL_ZIP_URL = 'https://alphacephei.com/vosk/models/vosk-model-small-ru-0.22.zip'
const VOSK_LIB_VERSION = '0.3.45'
const MIN_TAR_BYTES = 20 * 1024 * 1024

let pending = null
let native = { api: null, model: null }

function sendProgress(payload) {
  const win = BrowserWindow.getAllWindows()[0]
  if (win && !win.isDestroyed()) win.webContents.send('speech:vosk-progress', payload)
}

function voskRoot() {
  return path.join(app.getPath('userData'), 'vosk')
}

function tarPath() {
  return path.join(voskRoot(), `${MODEL_DIR_NAME}.tar.gz`)
}

function extractedDir() {
  return path.join(voskRoot(), MODEL_DIR_NAME)
}

function libDir() {
  return path.join(voskRoot(), 'lib')
}

function libFileName() {
  if (process.platform === 'win32') return 'libvosk.dll'
  if (process.platform === 'darwin') return 'libvosk.dylib'
  return 'libvosk.so'
}

function libFile() {
  return path.join(libDir(), libFileName())
}

function libZipUrl() {
  const ver = VOSK_LIB_VERSION
  if (process.platform === 'win32') return `https://github.com/alphacep/vosk-api/releases/download/v${ver}/vosk-win64-${ver}.zip`
  if (process.platform === 'darwin') {
    const arch = process.arch === 'arm64' ? 'osx-arm64' : 'osx-x86_64'
    return `https://github.com/alphacep/vosk-api/releases/download/v${ver}/vosk-${arch}-${ver}.zip`
  }
  return `https://github.com/alphacep/vosk-api/releases/download/v${ver}/vosk-linux-x86_64-${ver}.zip`
}

function tarReady() {
  try {
    return fs.existsSync(tarPath()) && fs.statSync(tarPath()).size >= MIN_TAR_BYTES
  } catch {
    return false
  }
}

function extractedReady() {
  const dir = extractedDir()
  return fs.existsSync(path.join(dir, 'am')) || fs.existsSync(path.join(dir, 'conf'))
}

function libReady() {
  return fs.existsSync(libFile())
}

function modelReady() {
  return extractedReady() || tarReady()
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
      const total = Number(response.headers['content-length'] || 0)
      const chunks = []
      let received = 0
      response.on('data', (chunk) => {
        chunks.push(chunk)
        received += chunk.length
        onBytes?.(received, total)
      })
      response.on('end', () => {
        fs.writeFileSync(dest, Buffer.concat(chunks))
        resolve(dest)
      })
      response.on('error', reject)
    })
    request.on('error', reject)
    request.end()
  })
}

function findLibSource(root) {
  const want = libFileName()
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
      else if (entry.name === want) return dir
    }
  }
  return ''
}

async function extractZip(zipFile, dest) {
  fs.mkdirSync(dest, { recursive: true })
  await runTar(['-xf', zipFile, '-C', dest])
}

async function ensureExtractedModel() {
  if (extractedReady()) return
  fs.mkdirSync(voskRoot(), { recursive: true })
  if (tarReady()) {
    sendProgress({ phase: 'extract', text: 'Распаковываю голосовую модель…' })
    await runTar(['-xzf', tarPath(), '-C', voskRoot()])
    if (extractedReady()) return
  }
  sendProgress({ phase: 'download', text: 'Скачиваю офлайн-модель русского голоса…', received: 0, total: 0 })
  const zipFile = path.join(voskRoot(), `${MODEL_DIR_NAME}.zip`)
  await downloadFile(MODEL_ZIP_URL, zipFile, (received, total) => {
    sendProgress({
      phase: 'download',
      text: 'Скачиваю офлайн-модель русского голоса…',
      received,
      total,
    })
  })
  sendProgress({ phase: 'extract', text: 'Распаковываю голосовую модель…' })
  if (fs.existsSync(extractedDir())) fs.rmSync(extractedDir(), { recursive: true, force: true })
  await extractZip(zipFile, voskRoot())
  try {
    fs.unlinkSync(zipFile)
  } catch {
    /* keep zip if locked */
  }
  if (!extractedReady()) throw new Error('Архив модели повреждён')
  if (!tarReady()) {
    const tmp = `${tarPath()}.part`
    if (fs.existsSync(tmp)) fs.unlinkSync(tmp)
    await runTar(['-czf', tmp, MODEL_DIR_NAME], voskRoot())
    fs.renameSync(tmp, tarPath())
  }
}

async function ensureNativeLib() {
  if (libReady()) return
  sendProgress({ phase: 'download', text: 'Скачиваю движок распознавания…', received: 0, total: 0 })
  const zipFile = path.join(voskRoot(), `vosk-lib-${VOSK_LIB_VERSION}.zip`)
  await downloadFile(libZipUrl(), zipFile, (received, total) => {
    sendProgress({
      phase: 'download',
      text: 'Скачиваю движок распознавания…',
      received,
      total,
    })
  })
  const unpack = path.join(voskRoot(), 'lib-unpack')
  if (fs.existsSync(unpack)) fs.rmSync(unpack, { recursive: true, force: true })
  await extractZip(zipFile, unpack)
  const source = findLibSource(unpack)
  if (!source) throw new Error('Не найден файл распознавания')
  fs.mkdirSync(libDir(), { recursive: true })
  for (const name of fs.readdirSync(source)) {
    const from = path.join(source, name)
    if (!fs.statSync(from).isFile()) continue
    fs.copyFileSync(from, path.join(libDir(), name))
  }
  try {
    fs.unlinkSync(zipFile)
    fs.rmSync(unpack, { recursive: true, force: true })
  } catch {
    /* ignore cleanup */
  }
  if (!libReady()) throw new Error('Движок распознавания не собрался')
}

function loadNative() {
  if (native.model) return native
  const koffi = require('koffi')
  if (process.platform === 'win32') {
    process.env.PATH = `${libDir()}${path.delimiter}${process.env.PATH || ''}`
  }
  const lib = koffi.load(libFile())
  const api = {
    setLogLevel: lib.func('void vosk_set_log_level(int level)'),
    modelNew: lib.func('void *vosk_model_new(str path)'),
    modelFree: lib.func('void vosk_model_free(void *model)'),
    recNew: lib.func('void *vosk_recognizer_new(void *model, float sample_rate)'),
    recAccept: lib.func('int vosk_recognizer_accept_waveform(void *rec, const void *data, int length)'),
    recFinal: lib.func('str vosk_recognizer_final_result(void *rec)'),
    recFree: lib.func('void vosk_recognizer_free(void *rec)'),
  }
  api.setLogLevel(-1)
  const model = api.modelNew(extractedDir())
  if (!model) throw new Error('Не удалось загрузить голосовую модель')
  native = { api, model }
  return native
}

function toPcmBuffer(raw) {
  if (!raw) return Buffer.alloc(0)
  if (Buffer.isBuffer(raw)) return raw
  if (raw instanceof ArrayBuffer) return Buffer.from(raw)
  if (ArrayBuffer.isView(raw)) {
    return Buffer.from(raw.buffer, raw.byteOffset, raw.byteLength)
  }
  if (raw.type === 'Buffer' && Array.isArray(raw.data)) return Buffer.from(raw.data)
  return Buffer.from(raw)
}

function transcribePcm(payload = {}) {
  const pcm = toPcmBuffer(payload.pcm || payload)
  if (pcm.length < 3200) return { ok: false, error: 'NO_AUDIO' }
  const engine = loadNative()
  const sampleRate = Number(payload.sampleRate) || 16000
  const rec = engine.api.recNew(engine.model, sampleRate)
  if (!rec) return { ok: false, error: 'Не удалось запустить распознавание' }
  try {
    const chunk = 16000
    for (let offset = 0; offset < pcm.length; offset += chunk) {
      const slice = pcm.subarray(offset, Math.min(pcm.length, offset + chunk))
      engine.api.recAccept(rec, slice, slice.length)
    }
    const json = engine.api.recFinal(rec) || '{}'
    let text = ''
    try {
      text = String(JSON.parse(json).text || '').trim()
    } catch {
      text = ''
    }
    return { ok: true, text }
  } finally {
    engine.api.recFree(rec)
  }
}

async function ensureVoskModel() {
  if (native.model) return { ok: true, native: true, url: 'mirefir-vosk://model.tar.gz', fileUrl: tarReady() ? pathToFileURL(tarPath()).href : '' }
  if (pending) return pending
  pending = (async () => {
    try {
      await ensureExtractedModel()
      await ensureNativeLib()
      sendProgress({ phase: 'load', text: 'Загружаю голосовую модель…' })
      loadNative()
      sendProgress({ phase: 'ready', text: '' })
      return {
        ok: true,
        native: true,
        url: 'mirefir-vosk://model.tar.gz',
        fileUrl: tarReady() ? pathToFileURL(tarPath()).href : '',
      }
    } catch (err) {
      sendProgress({ phase: 'error', text: err.message || 'Не удалось подготовить голос' })
      return { ok: false, error: err.message || 'Не удалось подготовить голос' }
    } finally {
      pending = null
    }
  })()
  return pending
}

function registerVoskProtocol(protocol) {
  protocol.handle('mirefir-vosk', async () => {
    if (!tarReady()) return new Response('missing', { status: 404 })
    return net.fetch(pathToFileURL(tarPath()).href)
  })
}

module.exports = { ensureVoskModel, registerVoskProtocol, modelReady, transcribePcm }
