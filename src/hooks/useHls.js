import { useEffect, useRef, useState } from 'react'
import Hls from 'hls.js'

function isHlsUrl(url) {
  return /\.m3u8(\?|$)/i.test(url) || url.toLowerCase().includes('m3u8')
}

function segmentTimeMs(url) {
  const match = String(url || '').match(/(\d{10,13})(?:\.ts|\.m4s)/i)
  if (!match) return 0
  const value = Number(match[1])
  if (!Number.isFinite(value) || value < 1e9) return 0
  return value > 1e12 ? value : value * 1000
}

function createEngine(compact = false, bufferSec = 15, vod = false) {
  const live = Math.min(45, Math.max(8, Number(bufferSec) || 15))
  return new Hls({
    enableWorker: true,
    lowLatencyMode: !vod,
    liveDurationInfinity: false,
    backBufferLength: compact ? 8 : vod ? Math.min(90, live * 2) : live,
    maxBufferLength: compact ? 6 : vod ? Math.min(40, live + 20) : Math.min(20, live),
    maxMaxBufferLength: compact ? 12 : vod ? Math.min(60, live + 30) : Math.min(30, live + 10),
    capLevelToPlayerSize: compact,
    startLevel: compact ? 0 : -1,
    liveSyncDurationCount: vod ? 1 : 3,
    startPosition: vod ? 0 : -1,
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
  const bufferSec = Number(options.bufferSec) || 15
  const requireVod = Boolean(options.requireVod)
  const liveUrl = options.liveUrl || ''
  const expectedSec = Number(options.expectedDurationSec) || 0
  const archiveStartMs = Number(options.archiveStartMs) || 0
  const fallbackKey = (options.fallbacks || []).join('\n')
  const onUnavailableRef = useRef(options.onUnavailable)
  onUnavailableRef.current = options.onUnavailable

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
    queueRef.current = uniqueUrls(src, fallbackKey ? fallbackKey.split('\n') : []).filter(
      (item) => !requireVod || !liveUrl || item !== liveUrl,
    )
    setCurrent(queueRef.current[0] || '')
  }, [fallbackKey, liveUrl, requireVod, src])

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
      const hls = createEngine(compact, bufferSec, requireVod)
      hlsRef.current = hls
      hls.attachMedia(video)

      const looksLikeLiveEdge = (details) => {
        const frags = details?.fragments || []
        const first = frags[0]
        const last = frags[frags.length - 1]
        const stamp = (frag) =>
          segmentTimeMs(frag?.relurl || frag?.url || '') || Number(frag?.programDateTime) || 0
        const firstMs = stamp(first)
        const lastMs = stamp(last)
        const edgeMs = lastMs || firstMs
        if (archiveStartMs && firstMs && Math.abs(firstMs - archiveStartMs) > 180000) return true
        if (edgeMs) return Date.now() - edgeMs <= 45000
        if (/[?&](utc|lutc)=/i.test(current)) return false
        if (/timeshift_abs|timeshift_rel|\/timeshift\//i.test(current)) return false
        return false
      }

      const rejectLive = () => {
        if (tryNext()) {
          hls.stopLoad()
          return
        }
        setLoading(false)
        setError('Сервер отдал прямой эфир вместо архива.')
        onUnavailableRef.current?.()
      }

      let netFails = 0
      hls.on(Hls.Events.MANIFEST_PARSED, () => {
        netFails = 0
        if (requireVod) {
          try {
            const start = video.seekable?.length ? video.seekable.start(0) : 0
            if (Number.isFinite(start)) video.currentTime = start
          } catch {
            /* ignore */
          }
        }
        play()
      })

      const checkLive = (details) => {
        if (!requireVod || id !== requestId.current || !hlsRef.current) return
        if (looksLikeLiveEdge(details)) rejectLive()
      }

      const liveChecks = []
      hls.on(Hls.Events.LEVEL_LOADED, (_event, data) => {
        if (!requireVod || id !== requestId.current) return
        liveChecks.push(window.setTimeout(() => checkLive(data.details), 800))
        liveChecks.push(window.setTimeout(() => checkLive(data.details), 2500))
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
      return () => {
        window.clearTimeout(watchdog)
        liveChecks.forEach((timer) => window.clearTimeout(timer))
      }
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
  }, [archiveStartMs, bufferSec, compact, current, expectedSec, requireVod, videoRef])

  return { error, loading }
}
