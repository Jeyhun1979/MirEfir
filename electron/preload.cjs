const { contextBridge, ipcRenderer } = require('electron')

contextBridge.exposeInMainWorld('oneplayer', {
  platform: 'electron',
  openPlaylistFile: () => ipcRenderer.invoke('playlist:open-file'),
  openExternal: (href) => ipcRenderer.invoke('shell:open-external', href),
  pickFolder: () => ipcRenderer.invoke('storage:pick-folder'),
  internalFolder: () => ipcRenderer.invoke('storage:internal-folder'),
  storageSpace: (folder) => ipcRenderer.invoke('storage:space', folder),
  writeChunk: (filePath, buffer) =>
    ipcRenderer.invoke('storage:write-chunk', { filePath, buffer: Buffer.from(buffer) }),
  readFile: (filePath) => ipcRenderer.invoke('storage:read-file', filePath),
  listRecordings: (folder) => ipcRenderer.invoke('storage:list-files', folder),
  fileUrl: (filePath) => ipcRenderer.invoke('storage:file-url', filePath),
})
