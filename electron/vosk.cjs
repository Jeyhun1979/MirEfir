const { app, BrowserWindow, net } = require('electron')
const fs = require('fs')
const path = require('path')
const { spawn } = require('child_process')
const { pathToFileURL } = require('url')

const MODEL_DIR_NAME = 'vosk-model-small-ru-0.22'
const MODEL_ZIP_URL = 'https://alphacephei.com/vosk/models/vosk-model-small-ru-0.22.zip'
const MIN_TAR_BYTES = 20 * 1024 * 1024

let pending = null

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

function modelReady() {
  try {
    return fs.existsSync(tarPath()) && fs.statSync(tarPath()).size >= MIN_TAR_BYTES
  } catch {
    return false
  }
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
        reject(new Error(`Не удалось скачать голосовую модель (${response.statusCode})`))
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

async function buildTarFromZip(zipFile) {
  const root = voskRoot()
  const unpacked = extractedDir()
  if (fs.existsSync(unpacked)) fs.rmSync(unpacked, { recursive: true, force: true })
  fs.mkdirSync(root, { recursive: true })
  sendProgress({ phase: 'extract', text: 'Распаковываю голосовую модель…' })
  await runTar(['-xf', zipFile, '-C', root])
  if (!fs.existsSync(path.join(unpacked, 'am')) && !fs.existsSync(path.join(unpacked, 'conf'))) {
    throw new Error('Архив модели повреждён')
  }
  const out = tarPath()
  const tmp = `${out}.part`
  if (fs.existsSync(tmp)) fs.unlinkSync(tmp)
  await runTar(['-czf', tmp, MODEL_DIR_NAME], root)
  fs.renameSync(tmp, out)
  try {
    fs.unlinkSync(zipFile)
  } catch {
    /* keep zip if locked */
  }
}

async function ensureVoskModel() {
  if (modelReady()) {
    return { ok: true, url: 'mirefir-vosk://model.tar.gz', fileUrl: pathToFileURL(tarPath()).href }
  }
  if (pending) return pending
  pending = (async () => {
    try {
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
      await buildTarFromZip(zipFile)
      if (!modelReady()) throw new Error('Модель не собралась')
      sendProgress({ phase: 'ready', text: '' })
      return { ok: true, url: 'mirefir-vosk://model.tar.gz', fileUrl: pathToFileURL(tarPath()).href }
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
    if (!modelReady()) return new Response('missing', { status: 404 })
    return net.fetch(pathToFileURL(tarPath()).href)
  })
}

module.exports = { ensureVoskModel, registerVoskProtocol, modelReady }
