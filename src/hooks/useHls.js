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
  if (code === 404) return 'Поток недоступен (404). Источник отдал битую ссылку.'
  if (code) return `Ошибка потока (${code}).`
  if (data?.type === Hls.ErrorTypes.NETWORK_ERROR) return 'Сеть не отдаёт поток. Проверьте URL или CORS.'
  if (data?.type === Hls.ErrorTypes.MEDIA_ERROR) return 'Не удалось декодировать поток.'
  return 'Не удалось запустить канал.'
}

export function useHls(videoRef, src, options = {}) {
  const hlsRef = useRef(null)
  const requestId = useRef(0)
  const [activeSrc, setActiveSrc] = useState(src)
  const [loading, setLoading] = useState(Boolean(src))
  const [error, setError] = useState('')

  if (src !== activeSrc) {
    setActiveSrc(src)
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
      if (hlsRef.current) {
        hlsRef.current.destroy()
        hlsRef.current = null
      }
    }
  }, [videoRef])

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

    if (!src) {
      hlsRef.current?.stopLoad()
      video.pause()
      setLoading(false)
      return undefined
    }

    if (isHlsUrl(src) && Hls.isSupported()) {
      let hls = hlsRef.current
      if (!hls) {
        hls = createEngine(Boolean(options.compact))
        hlsRef.current = hls
        hls.attachMedia(video)

        let netFails = 0
        hls.on(Hls.Events.MANIFEST_PARSED, () => {
          netFails = 0
          play()
        })

        hls.on(Hls.Events.ERROR, (_event, data) => {
          if (!data?.fatal) return

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
      } else {
        video.pause()
        hls.stopLoad()
      }

      setError('')
      hls.loadSource(src)
      play()
      const watchdog = window.setTimeout(() => {
        if (id !== requestId.current) return
        if (video.readyState < 2) {
          setLoading(false)
          setError((current) => current || 'Поток не запустился. Архив или сдвиг могли быть недоступны.')
        }
      }, 18000)
      return () => window.clearTimeout(watchdog)
    }

    if (hlsRef.current) {
      hlsRef.current.stopLoad()
    }

    const onError = () => {
      if (id !== requestId.current) return
      setLoading(false)
      setError('Браузер не смог открыть этот поток.')
    }

    video.src = src
    video.addEventListener('loadedmetadata', play, { once: true })
    video.addEventListener('error', onError)

    return () => {
      video.removeEventListener('error', onError)
    }
  }, [options.compact, src, videoRef])

  return { error, loading }
}
