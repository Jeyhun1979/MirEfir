const MIN_FREE = 400 * 1024 * 1024
let browserDirHandle = null

export function formatBytes(bytes) {
  if (!bytes) return '—'
  if (bytes < 1024 * 1024) return `${Math.round(bytes / 1024)} КБ`
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} МБ`
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} ГБ`
}

export function hasElectronStorage() {
  return Boolean(window.mirefir?.pickFolder)
}

export async function getInternalFolder() {
  if (window.mirefir?.internalFolder) {
    return window.mirefir.internalFolder()
  }
  if (!window.showDirectoryPicker) {
    throw new Error('В браузере выберите папку на диске — или откройте Electron-версию.')
  }
  browserDirHandle = await window.showDirectoryPicker({ mode: 'readwrite', startIn: 'videos' })
  return { path: browserDirHandle.name, freeBytes: 0, handle: browserDirHandle }
}

export async function pickStorageFolder() {
  if (window.mirefir?.pickFolder) {
    const result = await window.mirefir.pickFolder()
    if (!result?.path) return null
    if (result.freeBytes && result.freeBytes < MIN_FREE) {
      throw new Error(`На носителе мало места (${formatBytes(result.freeBytes)}). Выберите другой диск или флешку.`)
    }
    return result
  }

  if (!window.showDirectoryPicker) {
    throw new Error('Выбор папки недоступен. Запустите Electron-версию или Chrome/Edge.')
  }

  browserDirHandle = await window.showDirectoryPicker({ mode: 'readwrite', startIn: 'downloads' })
  return { path: browserDirHandle.name, freeBytes: 0, handle: browserDirHandle }
}

export async function appendChunk(folderPath, fileName, buffer) {
  if (window.mirefir?.writeChunk) {
    const filePath = `${folderPath.replace(/[\\/]$/, '')}\\${fileName}`.replace(/\//g, '\\')
    await window.mirefir.writeChunk(filePath, buffer)
    return filePath
  }

  if (!browserDirHandle) throw new Error('Сначала выберите диск или флешку')
  const file = await browserDirHandle.getFileHandle(fileName, { create: true })
  const writable = await file.createWritable({ keepExistingData: true })
  const current = await file.getFile()
  await writable.seek(current.size)
  await writable.write(buffer)
  await writable.close()
  return fileName
}

export async function listStoredRecordings(folderPath) {
  if (window.mirefir?.listRecordings && folderPath) {
    return window.mirefir.listRecordings(folderPath)
  }
  if (!browserDirHandle) return []
  const items = []
  for await (const [name, handle] of browserDirHandle.entries()) {
    if (handle.kind !== 'file' || !/\.(webm|mp4|ts|mkv)$/i.test(name)) continue
    const file = await handle.getFile()
    items.push({ name, path: name, bytes: file.size, mtime: file.lastModified })
  }
  return items.sort((a, b) => b.mtime - a.mtime)
}

export async function recordingPlayUrl(filePath) {
  if (window.mirefir?.fileUrl) {
    const href = await window.mirefir.fileUrl(filePath)
    if (href) return href
  }
  const buffer = await readStoredFile(filePath)
  return URL.createObjectURL(new Blob([buffer], { type: 'video/webm' }))
}

export async function readStoredFile(filePath) {
  if (window.mirefir?.readFile) {
    const buf = await window.mirefir.readFile(filePath)
    return buf instanceof ArrayBuffer ? buf : buf.buffer
  }
  if (!browserDirHandle) throw new Error('Нет доступа к носителю')
  const file = await (await browserDirHandle.getFileHandle(filePath)).getFile()
  return file.arrayBuffer()
}

export function safeFileName(channelName) {
  const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
  const clean = String(channelName || 'channel').replace(/[^\p{L}\p{N}]+/gu, '_').slice(0, 40)
  return `${clean}-${stamp}.webm`
}
