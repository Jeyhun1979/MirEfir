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
let recCache = { grammarKey: '', grammarRec: null, dictRec: null, sampleRate: 0 }

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
    if (/\.(lib|a|exp|pdb)$/i.test(name)) continue
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
    recNewGrm: null,
    setMaxAlt: null,
    recAccept: lib.func('int vosk_recognizer_accept_waveform(void *rec, const void *data, int length)'),
    recFinal: lib.func('str vosk_recognizer_final_result(void *rec)'),
    recFree: lib.func('void vosk_recognizer_free(void *rec)'),
  }
  try {
    api.recNewGrm = lib.func('void *vosk_recognizer_new_grm(void *model, float sample_rate, str grammar)')
  } catch {
    api.recNewGrm = null
  }
  try {
    api.setMaxAlt = lib.func('void vosk_recognizer_set_max_alternatives(void *rec, int max_alternatives)')
  } catch {
    api.setMaxAlt = null
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

function grammarJsonFrom(payload) {
  const phrases = Array.isArray(payload?.phrases)
    ? payload.phrases.map((item) => String(item || '').trim()).filter((item) => item.length >= 2 && item.length <= 80)
    : []
  if (!phrases.length) return ''
  const unique = []
  const seen = new Set()
  for (const phrase of phrases.slice(0, 1200)) {
    const key = phrase.toLowerCase()
    if (seen.has(key)) continue
    seen.add(key)
    unique.push(phrase)
  }
  if (!unique.some((item) => item === '[unk]')) unique.push('[unk]')
  return JSON.stringify(unique)
}

function pickTranscript(raw) {
  let parsed = {}
  try {
    parsed = JSON.parse(raw || '{}')
  } catch {
    parsed = {}
  }
  const alts = Array.isArray(parsed.alternatives) && parsed.alternatives.length
    ? parsed.alternatives
    : [{ text: parsed.text, confidence: 1 }]
  for (const alt of alts) {
    const text = String(alt?.text || '')
      .replace(/\[unk\]/gi, ' ')
      .replace(/\s+/g, ' ')
      .trim()
    const conf = Number(alt?.confidence)
    if (!text) continue
    if (Number.isFinite(conf) && conf > 0 && conf < 0.12) continue
    return text
  }
  return ''
}

function freeRec(engine, rec) {
  if (!rec) return
  try {
    engine.api.recFree(rec)
  } catch {
    /* already gone */
  }
}

function recognizerFor(engine, sampleRate, grammarJson) {
  if (grammarJson && engine.api.recNewGrm) {
    if (recCache.grammarRec && recCache.grammarKey === grammarJson && recCache.sampleRate === sampleRate) {
      return recCache.grammarRec
    }
    freeRec(engine, recCache.grammarRec)
    recCache.grammarRec = null
    recCache.grammarKey = ''
    let rec = null
    try {
      rec = engine.api.recNewGrm(engine.model, sampleRate, grammarJson)
    } catch {
      rec = null
    }
    if (rec) {
      try {
        engine.api.setMaxAlt?.(rec, 3)
      } catch {
        /* optional */
      }
      recCache.grammarRec = rec
      recCache.grammarKey = grammarJson
      recCache.sampleRate = sampleRate
      return rec
    }
  }
  if (recCache.dictRec && recCache.sampleRate === sampleRate) return recCache.dictRec
  freeRec(engine, recCache.dictRec)
  recCache.dictRec = engine.api.recNew(engine.model, sampleRate)
  recCache.sampleRate = sampleRate
  return recCache.dictRec
}

function runRecognizer(engine, rec, pcm) {
  if (!rec) return ''
  const chunk = 16000
  for (let offset = 0; offset < pcm.length; offset += chunk) {
    const slice = pcm.subarray(offset, Math.min(pcm.length, offset + chunk))
    engine.api.recAccept(rec, slice, slice.length)
  }
  return pickTranscript(engine.api.recFinal(rec) || '{}')
}

function transcribePcm(payload = {}) {
  const pcm = toPcmBuffer(payload.pcm || payload)
  if (pcm.length < 3200) return { ok: false, error: 'NO_AUDIO' }
  const engine = loadNative()
  const sampleRate = Number(payload.sampleRate) || 16000
  const grammarJson = grammarJsonFrom(payload)
  const grammarRec = recognizerFor(engine, sampleRate, grammarJson)
  let text = runRecognizer(engine, grammarRec, pcm)
  if (!text && grammarJson) text = runRecognizer(engine, recognizerFor(engine, sampleRate, ''), pcm)
  if (!grammarRec && !text) return { ok: false, error: 'Не удалось запустить распознавание' }
  return { ok: true, text }
}

function pruneVoskJunk() {
  try {
    for (const name of fs.readdirSync(libDir())) {
      if (!/\.(lib|a|exp|pdb)$/i.test(name)) continue
      fs.unlinkSync(path.join(libDir(), name))
    }
  } catch {
    /* lib dir may be missing */
  }
  if (!extractedReady()) return
  try {
    if (fs.existsSync(tarPath())) fs.unlinkSync(tarPath())
  } catch {
    /* keep tar if locked */
  }
}

async function ensureVoskModel() {
  if (native.model) {
    pruneVoskJunk()
    return { ok: true, native: true, url: 'mirefir-vosk://model.tar.gz', fileUrl: tarReady() ? pathToFileURL(tarPath()).href : '' }
  }
  if (pending) return pending
  pending = (async () => {
    try {
      await ensureExtractedModel()
      await ensureNativeLib()
      sendProgress({ phase: 'load', text: 'Загружаю голосовую модель…' })
      loadNative()
      pruneVoskJunk()
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
