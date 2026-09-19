import { useEffect, useRef, useState } from 'react'
import Hls from 'hls.js'

function isHlsUrl(url) {
  return /\.m3u8(\?|$)/i.test(url) || String(url || '').toLowerCase().includes('m3u8')
}

function looksLikeProgressiveFile(url) {
  return /\.(mp4|mkv|avi|webm|mov|mp3|aac)(\?|$)/i.test(String(url || ''))
}

function shouldUseHls(url) {
  if (!Hls.isSupported()) return false
  if (looksLikeProgressiveFile(url)) return false
  if (isHlsUrl(url)) return true
  return /^https?:/i.test(String(url || ''))
}

function codecSupported(mime) {
  if (!mime) return true
  try {
    return Boolean(typeof MediaSource !== 'undefined' && MediaSource.isTypeSupported?.(mime))
  } catch {
    return false
  }
}

function audioCanPlay(codec, mime) {
  const raw = `${codec || ''} ${mime || ''}`.toLowerCase()
  if (/ac-3|ec-3|eac3|dts/.test(raw)) return codecSupported(mime)
  if (/mp4a\.40\.2|mp4a\.40\.5|mp4a\.40\.29|opus/.test(raw)) return true
  if (/\baac\b/.test(raw) && !/ac-3|ec-3/.test(raw)) return true
  if (/mp2|mpga|\bmp1\b|mpeg-?[12]|mp4a\.40\.34|\bmp3\b|audio\/mpeg/.test(raw)) return false
  return !mime || codecSupported(mime)
}

function preferPlayableLevel(hls, levels) {
  const list = levels || []
  if (!list.length) return
  const score = (level) => {
    const video = `${level.videoCodec || ''}`
    const audio = `${level.audioCodec || ''}`
    let value = 0
    if (/avc|h264/i.test(video)) value += 4
    if (/hvc1|hev1|hevc/i.test(video)) value += codecSupported(`video/mp4; codecs="${video}"`) ? 3 : 0
    if (/mp4a|aac/i.test(audio)) value += 3
    if (/mp4a\.40\.34|mp3|mpga|mp2|ac-3|ec-3|eac3/i.test(audio)) value += 1
    return value
  }
  let best = 0
  for (let i = 1; i < list.length; i += 1) {
    if (score(list[i]) > score(list[best])) best = i
  }
  if (list.length < 2 && best === 0) return
  hls.startLevel = best
  hls.currentLevel = best
  hls.nextLevel = best
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
    lowLatencyMode: false,
    liveDurationInfinity: false,
    backBufferLength: compact ? 8 : vod ? Math.min(90, live * 2) : live,
    maxBufferLength: compact ? 8 : vod ? Math.min(40, live + 20) : Math.min(24, live + 8),
    maxMaxBufferLength: compact ? 14 : vod ? Math.min(60, live + 30) : Math.min(36, live + 12),
    capLevelToPlayerSize: compact,
    startLevel: compact ? 0 : -1,
    liveSyncDurationCount: vod ? 3 : 3,
    liveMaxLatencyDurationCount: vod ? 3 : 8,
    startPosition: vod ? 0 : -1,
    startFragPrefetch: true,
    testBandwidth: false,
    manifestLoadingMaxRetry: 2,
    levelLoadingMaxRetry: 2,
    fragLoadingMaxRetry: 4,
  })
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
      if (video.currentTime < 0.3) return
      setLoading(true)
    }
    const onReady = () => {
      setLoading(false)
      setError('')
    }

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
      video.play().catch(() => {
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

    if (shouldUseHls(current)) {
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
        if (archiveStartMs && firstMs && Math.abs(firstMs - archiveStartMs) <= 180000) return false
        if (archiveStartMs && firstMs && Math.abs(firstMs - archiveStartMs) > 180000) return true
        if (edgeMs) return Date.now() - edgeMs <= 12000
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
        setError('')
        onUnavailableRef.current?.()
      }

      let netFails = 0
      let mediaFails = 0
      let liveEndedTries = 0
      let shown = false
      let waitingSince = 0
      const recoverLiveEdge = () => {
        if (id !== requestId.current || !hlsRef.current) return
        const live = hls.liveSyncPosition
        try {
          if (Number.isFinite(live) && live >= 0) video.currentTime = live
          else hls.recoverMediaError()
          play()
        } catch {
          hls.startLoad()
        }
      }
      const firstPicture = () => {
        if (shown || id !== requestId.current) return
        shown = true
        waitingSince = 0
        setLoading(false)
        setError('')
        play()
      }
      hls.on(Hls.Events.MANIFEST_PARSED, (_event, data) => {
        netFails = 0
        preferPlayableLevel(hls, data?.levels)
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
      if (!requireVod && !compact) {
        hls.on(Hls.Events.FRAG_BUFFERED, firstPicture)
        hls.on(Hls.Events.FRAG_CHANGED, firstPicture)
      }
      if (Hls.Events.BUFFER_CODECS) {
        hls.on(Hls.Events.BUFFER_CODECS, (_event, data) => {
          const audio = data?.audio
          if (!audio) return
          const codec = `${audio.codec || ''}`
          const mime = audio.container
            ? `${audio.container}${codec ? `; codecs="${codec}"` : ''}`
            : ''
          if (audioCanPlay(codec, mime)) return
          delete data.audio
        })
      }

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
        if (data?.details === Hls.ErrorDetails?.BUFFER_STALLED_ERROR) {
          recoverLiveEdge()
          return
        }
        if (!data?.fatal) return
        if (tryNext()) {
          hls.stopLoad()
          return
        }
        if (!isHlsUrl(current) && !requireVod) {
          hls.stopLoad()
          destroyEngine(hlsRef)
          video.src = current
          play()
          return
        }
        const status = data.response?.code
        if (status === 404 || status === 410) {
          hls.stopLoad()
          setLoading(false)
          if (requireVod) onUnavailableRef.current?.()
          return
        }
        if (data.type === Hls.ErrorTypes.NETWORK_ERROR) {
          netFails += 1
          if (netFails > 2) {
            hls.stopLoad()
            setLoading(false)
            if (requireVod) onUnavailableRef.current?.()
            else hls.startLoad()
            return
          }
          hls.startLoad()
          return
        }
        if (data.type === Hls.ErrorTypes.MEDIA_ERROR) {
          mediaFails += 1
          if (mediaFails > 1 && tryNext()) {
            hls.stopLoad()
            return
          }
          hls.recoverMediaError()
          return
        }
        hls.stopLoad()
        setLoading(false)
        if (requireVod) onUnavailableRef.current?.()
      })

      const onEnded = () => {
        if (id !== requestId.current || requireVod) return
        if (liveEndedTries >= 1) return
        liveEndedTries += 1
        recoverLiveEdge()
      }
      video.addEventListener('ended', onEnded)

      setError('')
      setLoading(true)
      hls.loadSource(current)
      play()
      const stallWatch = window.setInterval(() => {
        if (id !== requestId.current) return
        if (video.paused || video.ended) {
          waitingSince = 0
          return
        }
        const stuck = video.readyState < 3 && video.currentTime > 0.4
        if (!stuck) {
          waitingSince = 0
          return
        }
        if (!waitingSince) waitingSince = Date.now()
        if (Date.now() - waitingSince >= 3500) {
          waitingSince = Date.now()
          recoverLiveEdge()
        }
      }, 700)
      const watchdog = window.setTimeout(() => {
        if (id !== requestId.current) return
        if (video.readyState >= 2 || video.currentTime > 0.2) {
          setLoading(false)
          setError('')
          return
        }
        if (tryNext()) return
        setLoading(false)
        if (requireVod) onUnavailableRef.current?.()
      }, 12000)
      return () => {
        video.removeEventListener('ended', onEnded)
        window.clearTimeout(watchdog)
        window.clearInterval(stallWatch)
        liveChecks.forEach((timer) => window.clearTimeout(timer))
      }
    }

    const onError = () => {
      if (id !== requestId.current) return
      if (tryNext()) return
      setLoading(false)
      if (requireVod) onUnavailableRef.current?.()
    }

    video.src = current
    video.addEventListener('loadedmetadata', play, { once: true })
    video.addEventListener('error', onError)

    return () => {
      video.removeEventListener('error', onError)
    }
  }, [archiveStartMs, bufferSec, compact, current, expectedSec, requireVod, videoRef])

  return { error, loading, clearError: () => setError('') }
}
