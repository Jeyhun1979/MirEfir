import { useEffect, useRef, useState } from 'react'
import Hls from 'hls.js'

function isHlsUrl(url) {
  return /\.m3u8(\?|$)/i.test(url) || url.toLowerCase().includes('m3u8')
}

function createEngine(compact = false) {
  return new Hls({
    enableWorker: true,
    lowLatencyMode: true,
    liveDurationInfinity: false,
    backBufferLength: compact ? 8 : 240,
    maxBufferLength: compact ? 6 : 30,
    maxMaxBufferLength: compact ? 12 : 60,
    capLevelToPlayerSize: compact,
    startLevel: compact ? 0 : -1,
    liveSyncDurationCount: 3,
    startFragPrefetch: true,
    testBandwidth: false,
    manifestLoadingMaxRetry: 1,
    levelLoadingMaxRetry: 1,
    fragLoadingMaxRetry: 2,
  })
}

function errorMessage(data) {
  const code = data?.response?.code
  if (code === 404 || code === 410) return 'Поток недоступен. Архив на этом канале мог быть недоступен.'
  if (code && code !== 200) return `Ошибка потока (${code}).`
  if (data?.type === Hls.ErrorTypes.NETWORK_ERROR) return 'Сеть не отдаёт поток. Проверьте URL или CORS.'
  if (data?.type === Hls.ErrorTypes.MEDIA_ERROR) return 'Не удалось декодировать поток.'
  return 'Не удалось запустить архив. Попробуйте другую передачу или канал.'
}

function destroyEngine(hlsRef) {
  if (!hlsRef.current) return
  hlsRef.current.stopLoad()
  hlsRef.current.detachMedia()
  hlsRef.current.destroy()
  hlsRef.current = null
}

function uniqueUrls(src, fallbacks) {
  const list = [src, ...(fallbacks || [])].filter(Boolean)
  return [...new Set(list)]
}

export function useHls(videoRef, src, options = {}) {
  const hlsRef = useRef(null)
  const requestId = useRef(0)
  const queueRef = useRef([])
  const [activeSrc, setActiveSrc] = useState(src)
  const [current, setCurrent] = useState(src)
  const [loading, setLoading] = useState(Boolean(src))
  const [error, setError] = useState('')
  const compact = Boolean(options.compact)
  const fallbackKey = (options.fallbacks || []).join('\n')

  if (src !== activeSrc) {
    setActiveSrc(src)
    setCurrent(src)
    setError('')
    setLoading(Boolean(src))
  }

  useEffect(() => {
    const video = videoRef.current
    if (!video) return undefined

    const onWaiting = () => {
      if (video.readyState >= 2 && video.currentTime > 0.3) return
      setLoading(true)
    }
    const onReady = () => setLoading(false)

    video.addEventListener('waiting', onWaiting)
    video.addEventListener('stalled', onWaiting)
    video.addEventListener('playing', onReady)
    video.addEventListener('canplay', onReady)

    return () => {
      video.removeEventListener('waiting', onWaiting)
      video.removeEventListener('stalled', onWaiting)
      video.removeEventListener('playing', onReady)
      video.removeEventListener('canplay', onReady)
      destroyEngine(hlsRef)
    }
  }, [videoRef])

  useEffect(() => {
    queueRef.current = uniqueUrls(src, fallbackKey ? fallbackKey.split('\n') : [])
    setCurrent(queueRef.current[0] || '')
  }, [fallbackKey, src])

  useEffect(() => {
    const video = videoRef.current
    if (!video) return undefined

    const id = ++requestId.current
    const play = () => {
      video.muted = false
      video.play().catch(() => {
        video.muted = false
        video.play().catch(() => {})
      })
    }

    const tryNext = () => {
      const next = queueRef.current.find((item) => item && item !== current)
      if (!next) return false
      queueRef.current = queueRef.current.filter((item) => item !== current)
      setCurrent(next)
      return true
    }

    destroyEngine(hlsRef)
    video.removeAttribute('src')
    video.load()

    if (!current) {
      setLoading(false)
      return undefined
    }

    if (isHlsUrl(current) && Hls.isSupported()) {
      const hls = createEngine(compact)
      hlsRef.current = hls
      hls.attachMedia(video)

      let netFails = 0
      hls.on(Hls.Events.MANIFEST_PARSED, () => {
        netFails = 0
        play()
      })

      hls.on(Hls.Events.ERROR, (_event, data) => {
        if (!data?.fatal) return
        if (tryNext()) {
          hls.stopLoad()
          return
        }
        const status = data.response?.code
        if (status === 404 || status === 410) {
          hls.stopLoad()
          setLoading(false)
          setError(errorMessage(data))
          return
        }
        if (data.type === Hls.ErrorTypes.NETWORK_ERROR) {
          netFails += 1
          if (netFails > 2) {
            hls.stopLoad()
            setLoading(false)
            setError(errorMessage(data))
            return
          }
          hls.startLoad()
          return
        }
        if (data.type === Hls.ErrorTypes.MEDIA_ERROR) {
          hls.recoverMediaError()
          return
        }
        hls.stopLoad()
        setLoading(false)
        setError(errorMessage(data))
      })

      setError('')
      setLoading(true)
      hls.loadSource(current)
      play()
      const watchdog = window.setTimeout(() => {
        if (id !== requestId.current) return
        if (video.readyState < 2 && !tryNext()) {
          setLoading(false)
          setError((prev) => prev || 'Поток не запустился. Архив или сдвиг могли быть недоступны.')
        }
      }, 12000)
      return () => window.clearTimeout(watchdog)
    }

    const onError = () => {
      if (id !== requestId.current) return
      if (tryNext()) return
      setLoading(false)
      setError('Браузер не смог открыть этот поток.')
    }

    video.src = current
    video.addEventListener('loadedmetadata', play, { once: true })
    video.addEventListener('error', onError)

    return () => {
      video.removeEventListener('error', onError)
    }
  }, [compact, current, videoRef])

  return { error, loading }
}
