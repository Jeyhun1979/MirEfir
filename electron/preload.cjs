const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('mirefir', {
  platform: 'electron',
  openPlaylistFile: () => ipcRenderer.invoke('playlist:open-file'),
  openExternal: (href) => ipcRenderer.invoke('shell:open-external', href),
  pickFolder: () => ipcRenderer.invoke('storage:pick-folder'),
  internalFolder: () => ipcRenderer.invoke('storage:internal-folder'),
  storageSpace: (folder) => ipcRenderer.invoke('storage:space', folder),
  writeChunk: (filePath, buffer) => ipcRenderer.invoke('storage:write-chunk', { filePath, buffer }),
  readFile: (filePath) => ipcRenderer.invoke('storage:read-file', filePath),
  listRecordings: (folder) => ipcRenderer.invoke('storage:list-files', folder),
  fileUrl: (filePath) => ipcRenderer.invoke('storage:file-url', filePath),
  quit: () => ipcRenderer.invoke('app:quit'),
  loadPersist: () => ipcRenderer.invoke('config:load'),
  savePersist: (data) => ipcRenderer.invoke('config:save', data),
  appInfo: () => ipcRenderer.invoke('app:info'),
  checkUpdate: () => ipcRenderer.invoke('update:check'),
  downloadUpdate: () => ipcRenderer.invoke('update:download'),
  applyUpdate: () => ipcRenderer.invoke('update:apply'),
  listenSpeech: (payload) => ipcRenderer.invoke('speech:listen', payload || {}),
  cancelSpeech: () => ipcRenderer.invoke('speech:cancel'),
  onUpdateProgress: (handler) => {
    const listen = (_event, data) => handler(data)
    ipcRenderer.on('update:progress', listen)
    return () => ipcRenderer.removeListener('update:progress', listen)
  },
})
