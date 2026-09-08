const { app, BrowserWindow, ipcMain, net } = require('electron')
const fs = require('fs')
const path = require('path')
const { spawn } = require('child_process')

const GITHUB_OWNER = 'Jeyhun1979'
const GITHUB_REPO = 'MirEfir'

let pendingUrl = ''
let pendingFile = ''
let applying = false

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

function pickSetup(assets) {
  const files = assets || []
  return files.find((item) => /\.exe$/i.test(item.name) && /setup/i.test(item.name)) || files.find((item) => /\.exe$/i.test(item.name))
}

function logUpdate(message) {
  try {
    const file = path.join(app.getPath('userData'), 'updater.log')
    fs.appendFileSync(file, `${new Date().toISOString()} ${message}\n`)
  } catch {
    /* ignore */
  }
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

function quote(value) {
  return `"${String(value).replace(/"/g, '')}"`
}

function applyDownloadedFile(filePath) {
  if (!filePath || !fs.existsSync(filePath)) throw new Error('Файл обновления не найден')
  applying = true
  const temp = app.getPath('temp')
  const bat = path.join(temp, 'mirefir-update.cmd')
  const vbs = path.join(temp, 'mirefir-update.vbs')
  const log = path.join(app.getPath('userData'), 'updater.log')
  const exe = process.execPath
  const setup = path.resolve(filePath)

  const lines = [
    '@echo off',
    'setlocal EnableExtensions',
    `echo apply-start %date% %time%>>${quote(log)}`,
    'ping 127.0.0.1 -n 4 >nul',
    `"%SystemRoot%\\System32\\taskkill.exe" /F /IM MirEfir.exe /T >>${quote(log)} 2>&1`,
    'ping 127.0.0.1 -n 4 >nul',
    `echo running-setup>>${quote(log)}`,
    `start /wait "" ${quote(setup)} /S /NCRC --updated`,
    `echo setup-exit %ERRORLEVEL%>>${quote(log)}`,
    'set waits=0',
    ':wait_setup',
    'tasklist /FO CSV /NH 2>nul | find /I "MirEfir-Setup" >nul',
    'if errorlevel 1 goto setup_gone',
    'set /a waits+=1',
    'if %waits% GEQ 90 goto setup_gone',
    'ping 127.0.0.1 -n 3 >nul',
    'goto wait_setup',
    ':setup_gone',
    `echo setup-gone waits=%waits%>>${quote(log)}`,
    `"%SystemRoot%\\System32\\taskkill.exe" /F /IM MirEfir.exe /T >>${quote(log)} 2>&1`,
    'ping 127.0.0.1 -n 4 >nul',
    `if exist ${quote(exe)} start "" ${quote(exe)}`,
    `echo relaunched>>${quote(log)}`,
    `del /f /q ${quote(setup)} >nul 2>&1`,
    `del /f /q ${quote(vbs)} >nul 2>&1`,
    'del /f /q "%~f0" >nul 2>&1',
  ]
  fs.writeFileSync(bat, lines.join('\r\n'), 'utf8')
  fs.writeFileSync(vbs, `CreateObject("WScript.Shell").Run ${JSON.stringify(bat)}, 0, False\r\n`, 'utf8')
  logUpdate(`spawn hidden installer ${setup}`)
  spawn('wscript.exe', ['//B', '//Nologo', vbs], {
    detached: true,
    stdio: 'ignore',
    windowsHide: true,
  }).unref()
  setTimeout(() => {
    for (const win of BrowserWindow.getAllWindows()) {
      win.removeAllListeners('close')
      if (!win.isDestroyed()) win.destroy()
    }
    app.exit(0)
  }, 800)
  return true
}

async function checkUpdate() {
  pendingUrl = ''
  pendingFile = ''
  if (!app.isPackaged) return null
  const data = await fetchJson(`https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases/latest`)
  const version = String(data.tag_name || data.name || '').replace(/^v/i, '')
  if (!version || !isNewer(version, app.getVersion())) return null
  const asset = pickSetup(data.assets || [])
  if (!asset?.browser_download_url) throw new Error('В релизе нет файла установки Windows')
  pendingUrl = asset.browser_download_url
  logUpdate(`found ${version} ${pendingUrl}`)
  return {
    version,
    notes: typeof data.body === 'string' ? data.body : '',
    downloadUrl: asset.browser_download_url,
  }
}

async function downloadUpdate() {
  if (pendingFile && fs.existsSync(pendingFile)) return pendingFile
  if (!pendingUrl) throw new Error('Нет файла обновления')
  const dest = path.join(app.getPath('temp'), path.basename(new URL(pendingUrl).pathname) || 'MirEfir-Setup.exe')
  pendingFile = await downloadToFile(pendingUrl, dest)
  logUpdate(`downloaded ${pendingFile}`)
  return pendingFile
}

async function applyUpdate() {
  if (!pendingFile) await downloadUpdate()
  return applyDownloadedFile(pendingFile)
}

function isApplyingUpdate() {
  return applying
}

function registerUpdateIpc() {
  ipcMain.handle('update:check', () => checkUpdate())
  ipcMain.handle('update:download', () => downloadUpdate())
  ipcMain.handle('update:apply', () => applyUpdate())
}

module.exports = { registerUpdateIpc, isApplyingUpdate }
