const { app, BrowserWindow, ipcMain, net } = require('electron')
const fs = require('fs')
const path = require('path')
const { spawn } = require('child_process')

const GITHUB_OWNER = 'Jeyhun1979'
const GITHUB_REPO = 'MirEfir'

let autoUpdater = null
let pendingFile = ''
let pendingMode = ''

try {
  ;({ autoUpdater } = require('electron-updater'))
} catch {
  autoUpdater = null
}

function sendProgress(payload) {
  const win = BrowserWindow.getAllWindows()[0]
  if (win && !win.isDestroyed()) win.webContents.send('update:progress', payload)
}

function isNewer(remote, local) {
  const a = String(remote || '')
    .replace(/^v/i, '')
    .split('.')
    .map((part) => Number.parseInt(part, 10) || 0)
  const b = String(local || '')
    .replace(/^v/i, '')
    .split('.')
    .map((part) => Number.parseInt(part, 10) || 0)
  for (let i = 0; i < Math.max(a.length, b.length); i += 1) {
    if ((a[i] || 0) > (b[i] || 0)) return true
    if ((a[i] || 0) < (b[i] || 0)) return false
  }
  return false
}

function pickSetup(assets, portable) {
  const files = assets || []
  if (portable) {
    return files.find((item) => /\.exe$/i.test(item.name) && !/setup/i.test(item.name))
  }
  return files.find((item) => /\.exe$/i.test(item.name) && /setup/i.test(item.name)) || files.find((item) => /\.exe$/i.test(item.name))
}

function fetchJson(url) {
  return new Promise((resolve, reject) => {
    const request = net.request({ url, redirect: 'follow' })
    request.setHeader('Accept', 'application/vnd.github+json')
    request.setHeader('User-Agent', 'MirEfir-Updater')
    const chunks = []
    request.on('response', (response) => {
      if (response.statusCode >= 400) {
        reject(new Error(`GitHub ответил ${response.statusCode}`))
        return
      }
      response.on('data', (chunk) => chunks.push(chunk))
      response.on('end', () => {
        try {
          resolve(JSON.parse(Buffer.concat(chunks).toString('utf8')))
        } catch (err) {
          reject(err)
        }
      })
      response.on('error', reject)
    })
    request.on('error', reject)
    request.end()
  })
}

function downloadToFile(url, dest) {
  return new Promise((resolve, reject) => {
    fs.mkdirSync(path.dirname(dest), { recursive: true })
    const request = net.request({ url, redirect: 'follow' })
    request.setHeader('User-Agent', 'MirEfir-Updater')
    request.on('response', (response) => {
      if (response.statusCode >= 400) {
        reject(new Error(`Сервер обновления ответил ${response.statusCode}`))
        return
      }
      const total = Number(response.headers['content-length']?.[0] || response.headers['content-length'] || 0)
      let received = 0
      const file = fs.createWriteStream(dest)
      response.on('data', (chunk) => {
        received += chunk.length
        file.write(chunk)
        sendProgress({ received, total })
      })
      response.on('end', () => {
        file.end(() => resolve(dest))
      })
      response.on('error', reject)
    })
    request.on('error', reject)
    request.end()
  })
}

async function checkGithubFallback() {
  const data = await fetchJson(`https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases/latest`)
  const version = String(data.tag_name || data.name || '').replace(/^v/i, '')
  if (!version || !isNewer(version, app.getVersion())) return null
  const portable = Boolean(process.env.PORTABLE_EXECUTABLE_DIR)
  const asset = pickSetup(data.assets || [], portable)
  if (!asset?.browser_download_url) throw new Error('В релизе нет файла установки Windows')
  pendingMode = 'file'
  pendingFile = asset.browser_download_url
  return {
    version,
    notes: typeof data.body === 'string' ? data.body : '',
    downloadUrl: asset.browser_download_url,
  }
}

function applyDownloadedFile(filePath) {
  if (!filePath || !fs.existsSync(filePath)) throw new Error('Файл обновления не найден')
  const bat = path.join(app.getPath('temp'), 'mirefir-update.cmd')
  const portableDir = process.env.PORTABLE_EXECUTABLE_DIR
  const exe = process.execPath
  const installDir = path.dirname(exe)
  const lines = [
    '@echo off',
    'timeout /t 3 /nobreak >nul',
    'taskkill /F /IM MirEfir.exe /T >nul 2>&1',
    'timeout /t 2 /nobreak >nul',
  ]
  if (portableDir && !/setup/i.test(path.basename(filePath))) {
    const target = path.join(portableDir, path.basename(exe))
    lines.push(`copy /y "${filePath}" "${target}"`)
    lines.push(`start "" "${target}"`)
  } else {
    lines.push(`start /wait "" "${filePath}" /S /NCRC /D=${installDir}`)
    lines.push(`start "" "${exe}"`)
  }
  lines.push(`del "${filePath}" >nul 2>&1`)
  lines.push('del "%~f0" >nul 2>&1')
  fs.writeFileSync(bat, lines.join('\r\n'), 'utf8')
  spawn('cmd.exe', ['/c', bat], { detached: true, stdio: 'ignore', windowsHide: true }).unref()
  setTimeout(() => {
    for (const win of BrowserWindow.getAllWindows()) {
      win.removeAllListeners('close')
      if (!win.isDestroyed()) win.destroy()
    }
    app.exit(0)
  }, 400)
  return true
}

function setupAutoUpdater() {
  if (!autoUpdater || !app.isPackaged) return
  autoUpdater.autoDownload = false
  autoUpdater.autoInstallOnAppQuit = false
  autoUpdater.allowDowngrade = false
  try {
    autoUpdater.setFeedURL({
      provider: 'github',
      owner: GITHUB_OWNER,
      repo: GITHUB_REPO,
    })
  } catch {
    /* keep defaults from package.json publish */
  }
  autoUpdater.on('download-progress', (progress) => {
    sendProgress({ received: progress.transferred || 0, total: progress.total || 0 })
  })
}

async function checkUpdate() {
  pendingFile = ''
  pendingMode = ''
  if (!app.isPackaged) return null

  if (autoUpdater) {
    try {
      const result = await autoUpdater.checkForUpdates()
      const info = result?.updateInfo
      const version = String(info?.version || '').replace(/^v/i, '')
      if (version && isNewer(version, app.getVersion())) {
        pendingMode = 'electron'
        return {
          version,
          notes: typeof info.releaseNotes === 'string' ? info.releaseNotes : '',
        }
      }
    } catch {
      /* no latest.yml yet — GitHub API fallback */
    }
  }

  return checkGithubFallback()
}

async function downloadUpdate() {
  if (pendingMode === 'electron' && autoUpdater) {
    await autoUpdater.downloadUpdate()
    return 'electron'
  }
  if (!pendingFile) throw new Error('Нет файла обновления')
  const dest = path.join(app.getPath('temp'), path.basename(new URL(pendingFile).pathname) || 'MirEfir-Setup.exe')
  pendingFile = await downloadToFile(pendingFile, dest)
  pendingMode = 'file'
  return pendingFile
}

async function applyUpdate() {
  if (pendingMode === 'electron' && autoUpdater) {
    autoUpdater.quitAndInstall(true, true)
    return true
  }
  return applyDownloadedFile(pendingFile)
}

function registerUpdateIpc() {
  setupAutoUpdater()
  ipcMain.handle('update:check', () => checkUpdate())
  ipcMain.handle('update:download', () => downloadUpdate())
  ipcMain.handle('update:apply', () => applyUpdate())
}

module.exports = { registerUpdateIpc }
