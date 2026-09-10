const { app, BrowserWindow, ipcMain, dialog, session, shell, protocol } = require('electron')
const path = require('path')
const fs = require('fs')
const { pathToFileURL } = require('url')
const { registerUpdateIpc, isApplyingUpdate, installInProgress, spawnInstallSplash } = require('./updater.cjs')
const { cancelWindowsSpeech } = require('./speech.cjs')
const { ensureVoskModel, registerVoskProtocol, transcribePcm } = require('./vosk.cjs')

protocol.registerSchemesAsPrivileged([
  {
    scheme: 'mirefir-vosk',
    privileges: {
      standard: true,
      secure: true,
      supportFetchAPI: true,
      corsEnabled: true,
      stream: true,
    },
  },
])

const DEV_URL = 'http://127.0.0.1:5173'
const CONFIG_NAME = 'mirefir-config.json'

function profileHasData(dir) {
  if (!dir || !fs.existsSync(dir)) return false
  return fs.existsSync(path.join(dir, 'Local Storage')) || fs.existsSync(path.join(dir, CONFIG_NAME))
}

function configPath() {
  return path.join(app.getPath('userData'), CONFIG_NAME)
}

function collectLegacyProfiles() {
  const found = []
  const portableDir = process.env.PORTABLE_EXECUTABLE_DIR
  if (portableDir) found.push(path.join(portableDir, 'MirEfir-data'))
  found.push(path.join(app.getPath('desktop'), 'MirEfir-data'))
  found.push(path.join(app.getPath('documents'), 'MirEfir-data'))
  found.push(path.join(app.getPath('downloads'), 'MirEfir-data'))
  return [...new Set(found)]
}

const stableUserData = path.join(app.getPath('appData'), 'MirEfir')
app.setPath('userData', stableUserData)

function migrateLegacyProfile() {
  if (profileHasData(stableUserData)) return
  for (const dir of collectLegacyProfiles()) {
    if (!profileHasData(dir)) continue
    try {
      fs.mkdirSync(stableUserData, { recursive: true })
      fs.cpSync(dir, stableUserData, { recursive: true, force: false })
      return
    } catch {
      /* try next folder */
    }
  }
}

const installingNow = process.platform === 'win32' && installInProgress()
if (installingNow) spawnInstallSplash()

const gotLock = installingNow ? false : app.requestSingleInstanceLock()
if (installingNow) {
  app.exit(0)
} else if (!gotLock) {
  app.exit(0)
} else {
  app.on('second-instance', () => {
    if (installInProgress()) return
    const win = BrowserWindow.getAllWindows()[0]
    if (!win) return
    if (win.isMinimized()) win.restore()
    win.show()
    win.focus()
  })
}

app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required')

function forceQuit() {
  if (isApplyingUpdate()) {
    app.exit(0)
    return
  }
  for (const win of BrowserWindow.getAllWindows()) {
    win.removeAllListeners('close')
    if (!win.isDestroyed()) win.destroy()
  }
  app.exit(0)
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1680,
    height: 960,
    minWidth: 1100,
    minHeight: 700,
    backgroundColor: '#06070a',
    title: 'MirEfir',
    autoHideMenuBar: true,
    show: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      webSecurity: false,
      backgroundThrottling: false,
    },
  })

  win.on('closed', () => {
    if (process.platform !== 'darwin') forceQuit()
  })

  if (!app.isPackaged) {
    win.loadURL(DEV_URL)
  } else {
    win.loadFile(path.join(__dirname, '../dist/index.html'))
  }
}

app.whenReady().then(() => {
  if (!gotLock) return
  migrateLegacyProfile()
  session.defaultSession.setPermissionRequestHandler((_wc, permission, callback) => {
    if (permission === 'openExternal') {
      callback(false)
      return
    }
    callback(true)
  })
  session.defaultSession.setPermissionCheckHandler((_wc, permission) => permission !== 'openExternal')
  if (typeof session.defaultSession.setDevicePermissionHandler === 'function') {
    session.defaultSession.setDevicePermissionHandler(() => true)
  }
  session.defaultSession.webRequest.onBeforeSendHeaders((details, callback) => {
    const headers = { ...details.requestHeaders }
    if (!headers['User-Agent'] && !headers['user-agent']) {
      headers['User-Agent'] = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) MirEfir/1.0'
    }
    callback({ requestHeaders: headers })
  })

  registerVoskProtocol(protocol)
  createWindow()
  registerUpdateIpc()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') forceQuit()
})

ipcMain.handle('storage:internal-folder', async () => {
  const folder = path.join(app.getPath('videos'), 'MirEfir')
  fs.mkdirSync(folder, { recursive: true })
  let freeBytes = 0
  try {
    const info = fs.statfsSync(folder)
    freeBytes = Number(info.bavail) * Number(info.bsize)
  } catch {
    /* older Node */
  }
  return { path: folder, freeBytes }
})

ipcMain.handle('storage:pick-folder', async () => {
  const result = await dialog.showOpenDialog({
    title: 'Выберите диск или флешку для записей',
    properties: ['openDirectory', 'createDirectory'],
  })
  if (result.canceled || !result.filePaths[0]) return null
  const folder = result.filePaths[0]
  let freeBytes = 0
  try {
    const info = fs.statfsSync(folder)
    freeBytes = Number(info.bavail) * Number(info.bsize)
  } catch {
    /* older Node */
  }
  return { path: folder, freeBytes }
})

ipcMain.handle('storage:space', async (_event, folder) => {
  try {
    const info = fs.statfsSync(folder)
    return { freeBytes: Number(info.bavail) * Number(info.bsize) }
  } catch {
    return { freeBytes: 0 }
  }
})

ipcMain.handle('storage:write-chunk', async (_event, payload) => {
  const filePath = payload.filePath
  const raw = payload.buffer
  const buffer = Buffer.isBuffer(raw) ? raw : Buffer.from(raw)
  fs.mkdirSync(path.dirname(filePath), { recursive: true })
  fs.appendFileSync(filePath, buffer)
  return { bytes: buffer.length }
})

ipcMain.handle('storage:read-file', async (_event, filePath) => {
  return fs.readFileSync(filePath)
})

ipcMain.handle('storage:file-url', async (_event, filePath) => {
  if (!filePath || !fs.existsSync(filePath)) return null
  return pathToFileURL(filePath).href
})

ipcMain.handle('storage:list-files', async (_event, folder) => {
  if (!folder || !fs.existsSync(folder)) return []
  return fs
    .readdirSync(folder)
    .filter((name) => /\.(webm|mp4|ts|mkv)$/i.test(name))
    .map((name) => {
      const full = path.join(folder, name)
      const stat = fs.statSync(full)
      return { name, path: full, bytes: stat.size, mtime: stat.mtimeMs }
    })
    .sort((a, b) => b.mtime - a.mtime)
})

ipcMain.handle('shell:open-external', async (_event, href) => {
  if (typeof href === 'string' && /^https?:\/\//i.test(href)) {
    await shell.openExternal(href)
  }
})

ipcMain.handle('shell:mic-settings', async () => {
  if (process.platform === 'win32') {
    await shell.openExternal('ms-settings:privacy-microphone')
  }
})

ipcMain.handle('app:quit', () => {
  forceQuit()
})

ipcMain.handle('config:load', async () => {
  try {
    return JSON.parse(fs.readFileSync(configPath(), 'utf8'))
  } catch {
    return null
  }
})

ipcMain.handle('config:save', async (_event, data) => {
  if (!data || typeof data !== 'object') return false
  fs.mkdirSync(app.getPath('userData'), { recursive: true })
  fs.writeFileSync(configPath(), JSON.stringify(data))
  return true
})

ipcMain.handle('app:info', () => ({
  packaged: app.isPackaged,
  portable: Boolean(process.env.PORTABLE_EXECUTABLE_DIR),
}))

ipcMain.handle('speech:listen', async () => ({ ok: false, error: 'NO_VOSK' }))
ipcMain.handle('speech:transcribe', async (_event, payload) => {
  try {
    const ready = await ensureVoskModel()
    if (!ready?.ok) return { ok: false, error: ready?.error || 'NO_VOSK' }
    return transcribePcm(payload)
  } catch (err) {
    return { ok: false, error: err.message || 'NO_VOSK' }
  }
})
ipcMain.handle('speech:ensure-vosk', async () => ensureVoskModel())
ipcMain.handle('speech:cancel', () => {
  cancelWindowsSpeech()
  return true
})

ipcMain.handle('playlist:open-file', async () => {
  const result = await dialog.showOpenDialog({
    title: 'Выберите M3U-плейлист',
    filters: [{ name: 'M3U Playlist', extensions: ['m3u', 'm3u8', 'txt'] }],
    properties: ['openFile'],
  })

  if (result.canceled || !result.filePaths[0]) return null

  const filePath = result.filePaths[0]
  return {
    name: path.basename(filePath),
    content: fs.readFileSync(filePath, 'utf8'),
  }
})
