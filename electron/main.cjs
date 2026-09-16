const { app, BrowserWindow, ipcMain, dialog, session, shell, protocol } = require('electron')
const path = require('path')
const fs = require('fs')
const { pathToFileURL } = require('url')
const { registerUpdateIpc, isApplyingUpdate, clearInstallLock, installInProgress } = require('./updater.cjs')
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

const startedFromUpdate = process.argv.includes('--updated') || installInProgress()
let focusResetTimer = 0

function activateOnWindows() {
  if (process.platform !== 'win32') return
  const { execFile } = require('child_process')
  execFile(
    'powershell.exe',
    [
      '-NoProfile',
      '-WindowStyle',
      'Hidden',
      '-Command',
      `try { (New-Object -ComObject WScript.Shell).AppActivate(${process.pid}) | Out-Null } catch {}`,
    ],
    { windowsHide: true },
    () => {},
  )
}

function focusWindow(win, { sticky = false } = {}) {
  if (!win || win.isDestroyed()) return
  if (win.isMinimized()) win.restore()
  win.show()
  win.flashFrame(false)
  if (typeof win.moveTop === 'function') win.moveTop()
  try {
    win.setAlwaysOnTop(true, 'screen-saver')
  } catch {
    win.setAlwaysOnTop(true)
  }
  win.focus()
  if (typeof app.focus === 'function') app.focus({ steal: true })
  activateOnWindows()
  const hold = sticky || startedFromUpdate ? 4500 : 900
  clearTimeout(focusResetTimer)
  focusResetTimer = setTimeout(() => {
    if (!win.isDestroyed()) win.setAlwaysOnTop(false)
  }, hold)
}

function scheduleFocus(win) {
  const delays = startedFromUpdate ? [0, 300, 900, 2000, 4000] : [0, 250, 1200]
  for (const ms of delays) {
    setTimeout(() => focusWindow(win, { sticky: startedFromUpdate }), ms)
  }
}

const gotLock = app.requestSingleInstanceLock()
if (!gotLock) {
  app.exit(0)
} else {
  app.on('second-instance', () => {
    const win = BrowserWindow.getAllWindows()[0]
    if (!win) return
    if (win.isFullScreen()) win.setFullScreen(true)
    focusWindow(win)
  })
}

app.commandLine.appendSwitch('autoplay-policy', 'no-user-gesture-required')
app.commandLine.appendSwitch('disk-cache-size', String(100 * 1024 * 1024))

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

function wireWindowIpc(win) {
  const sendFs = () => {
    if (!win.isDestroyed()) win.webContents.send('window:fullscreen', win.isFullScreen())
  }
  win.on('enter-full-screen', sendFs)
  win.on('leave-full-screen', sendFs)
  win.on('maximize', () => {
    if (win.isDestroyed() || win.isFullScreen()) return
    win.unmaximize()
    win.setFullScreen(true)
  })
}

function createWindow() {
  const win = new BrowserWindow({
    width: 1680,
    height: 960,
    minWidth: 1100,
    minHeight: 700,
    backgroundColor: '#06070a',
    title: 'MirEfir',
    frame: false,
    autoHideMenuBar: true,
    fullscreenable: true,
    show: true,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      webSecurity: false,
      backgroundThrottling: false,
    },
  })

  win.setMenuBarVisibility(false)
  wireWindowIpc(win)
  scheduleFocus(win)
  win.once('ready-to-show', () => focusWindow(win, { sticky: startedFromUpdate }))
  win.webContents.once('did-finish-load', () => focusWindow(win, { sticky: startedFromUpdate }))
  win.on('show', () => {
    if (startedFromUpdate) focusWindow(win, { sticky: true })
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
  if (typeof app.setAppUserModelId === 'function') app.setAppUserModelId('com.mirefir.app')
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
  migrateLegacyProfile()
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

function fromSender(event) {
  return BrowserWindow.fromWebContents(event.sender)
}

ipcMain.handle('window:minimize', (event) => {
  fromSender(event)?.minimize()
})

ipcMain.handle('window:toggle-fullscreen', (event) => {
  const win = fromSender(event)
  if (!win) return false
  const next = !win.isFullScreen()
  win.setFullScreen(next)
  const notify = () => {
    if (!win.isDestroyed()) win.webContents.send('window:fullscreen', win.isFullScreen())
  }
  setTimeout(notify, 0)
  setTimeout(notify, 80)
  return next
})

ipcMain.handle('window:set-fullscreen', (event, on) => {
  const win = fromSender(event)
  if (!win) return false
  const next = Boolean(on)
  win.setFullScreen(next)
  const notify = () => {
    if (!win.isDestroyed()) win.webContents.send('window:fullscreen', win.isFullScreen())
  }
  setTimeout(notify, 0)
  setTimeout(notify, 80)
  return next
})

ipcMain.handle('window:is-fullscreen', (event) => Boolean(fromSender(event)?.isFullScreen()))

ipcMain.handle('window:close', (event) => {
  const win = fromSender(event)
  if (!win) return
  win.close()
})

ipcMain.handle('app:launch-flags', async () => ({ fromUpdate: startedFromUpdate }))

ipcMain.handle('app:clear-install-lock', async () => {
  clearInstallLock()
  return true
})

function epgCachePath() {
  return path.join(app.getPath('userData'), 'epg-cache.json.gz')
}

function epgMetaPath() {
  return path.join(app.getPath('userData'), 'epg-cache-meta.json')
}

ipcMain.handle('epg:meta', async () => {
  try {
    return JSON.parse(fs.readFileSync(epgMetaPath(), 'utf8'))
  } catch {
    return null
  }
})

ipcMain.handle('epg:load-cache', async () => {
  try {
    return await fs.promises.readFile(epgCachePath())
  } catch {
    return null
  }
})

ipcMain.handle('epg:save-cache', async (_event, payload) => {
  if (!payload?.bytes) return false
  const buffer = Buffer.isBuffer(payload.bytes) ? payload.bytes : Buffer.from(payload.bytes)
  fs.mkdirSync(app.getPath('userData'), { recursive: true })
  const tmp = `${epgCachePath()}.tmp`
  fs.writeFileSync(tmp, buffer)
  fs.renameSync(tmp, epgCachePath())
  fs.writeFileSync(epgMetaPath(), JSON.stringify(payload.meta || { savedAt: Date.now() }))
  return true
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

ipcMain.handle('backup:save', async (_event, payload) => {
  const fileName = payload?.fileName || `mirefir-backup-${new Date().toISOString().slice(0, 10)}.json`
  const text = typeof payload?.text === 'string' ? payload.text : JSON.stringify(payload?.data || {}, null, 2)
  const result = await dialog.showSaveDialog({
    title: 'Сохранить резервную копию',
    defaultPath: path.join(app.getPath('documents'), fileName),
    filters: [{ name: 'JSON', extensions: ['json'] }],
  })
  if (result.canceled || !result.filePath) return { ok: false, canceled: true }
  fs.writeFileSync(result.filePath, text, 'utf8')
  return { ok: true, path: result.filePath }
})

ipcMain.handle('backup:open', async () => {
  const result = await dialog.showOpenDialog({
    title: 'Открыть резервную копию',
    properties: ['openFile'],
    filters: [
      { name: 'JSON', extensions: ['json'] },
      { name: 'Все файлы', extensions: ['*'] },
    ],
  })
  if (result.canceled || !result.filePaths[0]) return null
  const filePath = result.filePaths[0]
  return {
    name: path.basename(filePath),
    text: fs.readFileSync(filePath, 'utf8'),
  }
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
