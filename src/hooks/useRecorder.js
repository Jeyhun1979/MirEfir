import { useCallback, useEffect, useRef, useState } from 'react'
import { appendChunk, getInternalFolder, listStoredRecordings, pickStorageFolder, safeFileName } from '../lib/storage.js'

const META_KEY = 'mirefir.recordings'

function readMeta() {
  try {
    return JSON.parse(localStorage.getItem(META_KEY) || '[]')
  } catch {
    return []
  }
}

export function useRecorder(videoRef, channel, settings, updateSettings) {
  const recorderRef = useRef(null)
  const fileRef = useRef('')
  const [active, setActive] = useState(false)
  const [error, setError] = useState('')
  const [askWhere, setAskWhere] = useState(false)
  const [items, setItems] = useState(readMeta)

  const refreshDisk = useCallback(async () => {
    if (!settings.recordingPath) return
    try {
      const disk = await listStoredRecordings(settings.recordingPath)
      if (disk?.length) {
        setItems((current) => {
          const names = new Set(current.map((item) => item.file))
          const extra = disk
            .filter((item) => !names.has(item.path) && !names.has(item.name))
            .map((item) => ({
              id: item.path,
              title: item.name,
              file: item.path,
              bytes: item.bytes,
              ended: item.mtime,
            }))
          return extra.length ? [...extra, ...current] : current
        })
      }
    } catch {
      /* folder may be empty */
    }
  }, [settings.recordingPath])

  useEffect(() => {
    refreshDisk()
  }, [refreshDisk])

  const stop = useCallback(() => {
    const rec = recorderRef.current
    recorderRef.current = null
    if (rec && rec.state !== 'inactive') rec.stop()
    setActive(false)
  }, [])

  const begin = useCallback(
    async (folder) => {
      setError('')
      if (!settings.recordingsEnabled) throw new Error('Запись выключена в настройках')
      if (!channel) throw new Error('Сначала выберите канал')

      const video = videoRef.current
      if (!video) throw new Error('Плеер ещё не готов')

      if (video.readyState < 2) {
        await new Promise((resolve, reject) => {
          const timer = window.setTimeout(() => reject(new Error('Плеер ещё не готов к записи')), 8000)
          const done = () => {
            window.clearTimeout(timer)
            resolve()
          }
          video.addEventListener('playing', done, { once: true })
          video.addEventListener('loadeddata', done, { once: true })
        })
      }

      const capture = video.captureStream || video.mozCaptureStream
      if (!capture) throw new Error('Запись с этого устройства недоступна')

      let stream
      try {
        stream = capture.call(video)
      } catch {
        throw new Error('Не удалось захватить поток. Подождите, пока картинка появится, и нажмите запись снова.')
      }
      if (!stream.getVideoTracks().length) {
        await new Promise((resolve) => window.setTimeout(resolve, 400))
        stream = capture.call(video)
      }
      if (!stream.getVideoTracks().length) {
        throw new Error('Этот поток нельзя записать, пока нет картинки. Дождитесь эфира и повторите.')
      }
      const mime = MediaRecorder.isTypeSupported('video/webm;codecs=vp9,opus')
        ? 'video/webm;codecs=vp9,opus'
        : MediaRecorder.isTypeSupported('video/webm;codecs=vp8,opus')
          ? 'video/webm;codecs=vp8,opus'
          : 'video/webm'
      const rec = new MediaRecorder(stream, { mimeType: mime, videoBitsPerSecond: 3_500_000 })
      const fileName = safeFileName(channel.displayName || channel.name)
      fileRef.current =
        folder.includes('/') || folder.includes('\\') ? `${folder.replace(/[\\/]$/, '')}\\${fileName}` : fileName

      rec.ondataavailable = async (event) => {
        if (!event.data?.size) return
        try {
          await appendChunk(folder, fileName, await event.data.arrayBuffer())
        } catch (err) {
          setError(err.message)
          stop()
        }
      }
      rec.onstop = () => {
        const entry = {
          id: `${Date.now()}`,
          title: `${channel.displayName} · ${new Date().toLocaleString('ru-RU')}`,
          file: fileRef.current,
          channel: channel.displayName,
          ended: Date.now(),
        }
        setItems((current) => {
          const next = [entry, ...current].slice(0, 80)
          localStorage.setItem(META_KEY, JSON.stringify(next))
          return next
        })
      }

      recorderRef.current = rec
      rec.start(2000)
      setActive(true)
    },
    [channel, settings.recordingsEnabled, stop, videoRef],
  )

  const start = useCallback(async () => {
    setError('')
    if (!settings.recordingsEnabled) throw new Error('Запись выключена в настройках')
    if (!channel) throw new Error('Сначала выберите канал')
    if (!settings.recordingPath) {
      setAskWhere(true)
      return
    }
    await begin(settings.recordingPath)
  }, [begin, channel, settings.recordingPath, settings.recordingsEnabled])

  const pickInternal = useCallback(async () => {
    const picked = await getInternalFolder()
    if (!picked?.path) return
    updateSettings({ recordingPath: picked.path, recordingsEnabled: true })
    setAskWhere(false)
    await begin(picked.path)
  }, [begin, updateSettings])

  const pickExternal = useCallback(async () => {
    const picked = await pickStorageFolder()
    if (!picked?.path) return
    updateSettings({ recordingPath: picked.path, recordingsEnabled: true })
    setAskWhere(false)
    await begin(picked.path)
  }, [begin, updateSettings])

  return {
    active,
    error,
    items,
    askWhere,
    start,
    stop,
    pickInternal,
    pickExternal,
    cancelAsk: () => setAskWhere(false),
    refreshDisk,
  }
}
