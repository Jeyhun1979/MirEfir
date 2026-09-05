const { app, BrowserWindow, ipcMain, dialog, session, shell } = require('electron')
const path = require('path')
const fs = require('fs')
const { pathToFileURL } = require('url')

const DEV_URL = 'http://127.0.0.1:5173'

function createWindow() {
  const win = new BrowserWindow({
    width: 1680,
    height: 960,
    minWidth: 1100,
    minHeight: 700,
    backgroundColor: '#06070a',
    title: 'MirEfir',
    autoHideMenuBar: true,
    show: false,
    webPreferences: {
      preload: path.join(__dirname, 'preload.cjs'),
      contextIsolation: true,
      nodeIntegration: false,
      webSecurity: false,
    },
  })

  win.once('ready-to-show', () => win.show())

  if (!app.isPackaged) {
    win.loadURL(DEV_URL)
  } else {
    win.loadFile(path.join(__dirname, '../dist/index.html'))
  }
}

app.whenReady().then(() => {
  session.defaultSession.webRequest.onBeforeSendHeaders((details, callback) => {
    const headers = { ...details.requestHeaders }
    if (!headers['User-Agent'] && !headers['user-agent']) {
      headers['User-Agent'] = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) MirEfir/1.0'
    }
    callback({ requestHeaders: headers })
  })

  createWindow()

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) createWindow()
  })
})

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') app.quit()
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
  const buffer = Buffer.from(payload.buffer)
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
