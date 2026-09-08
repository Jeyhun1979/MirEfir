import { useEffect, useRef, useState } from 'react'
import { formatRange } from '../lib/epg.js'
import { useClock } from '../hooks/useClock.js'
import { useHls } from '../hooks/useHls.js'
import { useRecorder } from '../hooks/useRecorder.js'
import { usePlayer } from '../store/PlayerContext.jsx'
import { ClockOverlay } from './ClockOverlay.jsx'
import { LogoMark } from './LogoMark.jsx'
import { arrowDir, isBackKey, isOkKey } from '../lib/remoteKeys.js'

const PAD_MS = 6500
const NUDGE_MS = 15000

function formatHms(ms) {
  const sec = Math.max(0, Math.floor(Number(ms) / 1000) || 0)
  const hours = Math.floor(sec / 3600)
  const minutes = Math.floor((sec % 3600) / 60)
  const seconds = sec % 60
  return `${String(hours).padStart(2, '0')}:${String(minutes).padStart(2, '0')}:${String(seconds).padStart(2, '0')}`
}

export function VideoPlayer({ fullscreen = false }) {
  const videoRef = useRef(null)
  const now = useClock(1000)
  const {
    selectedChannel,
    streamUrl,
    playback,
    playProgram,
    seekArchive,
    seekToMs,
    selectChannel,
    focusZone,
    setFocusZone,
    setIsFullscreen,
    getCurrentProgram,
    getNextProgram,
    volume,
    muted,
    volumeTick,
    uiScreen,
    settings,
    updateSettings,
    recordingActive,
    setRecordingActive,
    recordPulse,
    pipPulse,
    liveGuideOpen,
    openMenu,
    requestVoiceSearch,
    toggleLiveGuide,
  } = usePlayer()
  const program = getCurrentProgram(selectedChannel)
  const nextProgram = getNextProgram(selectedChannel)
  const pauseForMulti = uiScreen === 'multiview' && !recordingActive
  const { error, loading } = useHls(videoRef, pauseForMulti ? '' : streamUrl)
  const recorder = useRecorder(videoRef, selectedChannel, settings, updateSettings)
  const [showVolume, setShowVolume] = useState(false)
  const [recHint, setRecHint] = useState('')
  const [pipOn, setPipOn] = useState(false)
  const [padOn, setPadOn] = useState(false)
  const [padFocus, setPadFocus] = useState('none')
  const [buttonCursor, setButtonCursor] = useState(0)
  const [previewMs, setPreviewMs] = useState(null)
  const [seekHud, setSeekHud] = useState(null)
  const lastPulse = useRef(0)
  const lastPip = useRef(0)
  const padTimer = useRef(0)
  const draggingRef = useRef(false)
  const barRef = useRef(null)
  const padActionsRef = useRef([])

  const archive = playback?.mode === 'archive'
  const boundsStart = archive ? playback.originStart || playback.start : program?.start
  const boundsEnd = archive ? playback.originEnd || playback.end : program?.end
  const hasBounds = Boolean(boundsStart && boundsEnd && boundsEnd > boundsStart)
  const durationMs = hasBounds ? boundsEnd - boundsStart : 0
  const liveMs = now.getTime()
  const videoTimeMs =
    archive && videoRef.current && Number.isFinite(videoRef.current.currentTime)
      ? (playback.start || boundsStart) + videoRef.current.currentTime * 1000
      : archive
        ? playback.start
        : liveMs
  const actualMs = hasBounds ? Math.min(boundsEnd, Math.max(boundsStart, videoTimeMs)) : liveMs
  const maxMs = archive ? boundsEnd : Math.min(boundsEnd || liveMs, liveMs)
  const displayMs = previewMs == null ? actualMs : previewMs
  const progress = hasBounds ? Math.min(1, Math.max(0, (actualMs - boundsStart) / durationMs)) : 0
  const displayProgress = hasBounds ? Math.min(1, Math.max(0, (displayMs - boundsStart) / durationMs)) : 0

  const showPad = () => {
    setPadOn(true)
    if (draggingRef.current) return
    window.clearTimeout(padTimer.current)
    padTimer.current = window.setTimeout(() => {
      setPadOn(false)
      setPadFocus('none')
      setButtonCursor(0)
      setPreviewMs(null)
    }, PAD_MS)
  }

  const clampMs = (value) => {
    if (!hasBounds) return value
    return Math.min(maxMs, Math.max(boundsStart, value))
  }

  const commitSeek = (targetMs) => {
    if (!hasBounds) return
    const next = clampMs(targetMs)
    const video = videoRef.current
    if (video) {
      const ranges = video.seekable
      if (ranges.length) {
        const start = ranges.start(0)
        const end = ranges.end(ranges.length - 1)
        const mediaTime = archive
          ? video.currentTime + (next - actualMs) / 1000
          : end - (Date.now() - next) / 1000
        if (mediaTime >= start - 0.05 && mediaTime <= end + 0.25) {
          video.currentTime = Math.min(end, Math.max(start, mediaTime))
          return
        }
      }
    }
    if (!archive && Date.now() - next < 2500 && selectedChannel) {
      selectChannel(selectedChannel.id)
      return
    }
    seekToMs(next, {
      originStart: boundsStart,
      originEnd: boundsEnd,
      start: boundsStart,
      end: boundsEnd,
      title: program?.title || playback?.title,
    })
  }

  const msFromClientX = (clientX) => {
    const node = barRef.current
    if (!node || !hasBounds) return actualMs
    const rect = node.getBoundingClientRect()
    const ratio = Math.min(1, Math.max(0, (clientX - rect.left) / rect.width))
    return clampMs(boundsStart + ratio * durationMs)
  }

  const startBarDrag = (clientX) => {
    if (!hasBounds) return
    draggingRef.current = true
    window.clearTimeout(padTimer.current)
    setPadFocus('bar')
    showPad()
    const next = msFromClientX(clientX)
    setPreviewMs(next)
  }

  const moveBarDrag = (clientX) => {
    if (!draggingRef.current || !hasBounds) return
    const next = msFromClientX(clientX)
    setPreviewMs(next)
  }

  const endBarDrag = () => {
    if (!draggingRef.current) return
    draggingRef.current = false
    if (previewMs != null) commitSeek(previewMs)
    setPreviewMs(null)
    showPad()
  }

  useEffect(() => {
    const video = videoRef.current
    if (!video) return
    video.volume = volume
    video.muted = muted
  }, [volume, muted])

  useEffect(() => {
    const onPad = () => {
      setPadFocus('none')
      showPad()
    }
    const onPadFocus = () => {
      setPadFocus('buttons')
      setButtonCursor(0)
      showPad()
    }
    window.addEventListener('mirefir:pad', onPad)
    window.addEventListener('mirefir:pad-focus', onPadFocus)
    return () => {
      window.removeEventListener('mirefir:pad', onPad)
      window.removeEventListener('mirefir:pad-focus', onPadFocus)
    }
  }, [])

  useEffect(() => {
    if (!padOn || liveGuideOpen || uiScreen) return undefined
    const onKey = (event) => {
      const dir = arrowDir(event)
      if (isBackKey(event)) {
        event.preventDefault()
        event.stopPropagation()
        setPadOn(false)
        setPadFocus('none')
        setPreviewMs(null)
        return
      }
      if (padFocus === 'none') {
        if (dir === 'down') {
          event.preventDefault()
          event.stopPropagation()
          setPadFocus('buttons')
          setButtonCursor(0)
          showPad()
        }
        return
      }
      if (dir === 'up' || dir === 'down') {
        event.preventDefault()
        event.stopPropagation()
        if (dir === 'down') setPadFocus(padFocus === 'buttons' ? 'bar' : 'bar')
        else setPadFocus(padFocus === 'bar' ? 'buttons' : 'none')
        showPad()
        return
      }
      if (dir === 'left' || dir === 'right') {
        event.preventDefault()
        event.stopPropagation()
        showPad()
        if (padFocus === 'bar' && hasBounds) {
          const next = clampMs(displayMs + (dir === 'right' ? NUDGE_MS : -NUDGE_MS))
          setPreviewMs(next)
          commitSeek(next)
          window.setTimeout(() => setPreviewMs(null), 400)
          return
        }
        setButtonCursor((current) => {
          const count = padActionsRef.current.length || 1
          return (current + (dir === 'right' ? 1 : -1) + count) % count
        })
        return
      }
      if (isOkKey(event)) {
        event.preventDefault()
        event.stopPropagation()
        showPad()
        if (padFocus === 'buttons') padActionsRef.current[buttonCursor]?.run()
      }
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [buttonCursor, displayMs, hasBounds, liveGuideOpen, padFocus, padOn, uiScreen])

  useEffect(() => {
    const seekInBuffer = (video, seconds) => {
      const ranges = video.seekable
      if (ranges.length) {
        const start = ranges.start(0)
        const end = ranges.end(ranges.length - 1)
        const next = Math.min(end, Math.max(start, video.currentTime + seconds))
        if (Math.abs(next - video.currentTime) < 0.2) return seconds > 0 ? 'live-edge' : 'start'
        video.currentTime = next
        return 'ok'
      }
      const duration = video.duration
      if (Number.isFinite(duration) && duration > 0) {
        video.currentTime = Math.min(duration, Math.max(0, video.currentTime + seconds))
        return 'ok'
      }
      const guess = video.currentTime + seconds
      if (guess >= 0) {
        video.currentTime = guess
        return 'ok'
      }
      return 'fail'
    }

    const applySeek = (seconds) => {
      const video = videoRef.current
      if (!video || !seconds) return
      const archive = playback?.mode === 'archive'
      const result = seekInBuffer(video, seconds)
      if (result === 'ok') {
        setSeekHud({ label: `${seconds > 0 ? '+' : '−'}${Math.abs(seconds)} сек`, at: Date.now() })
        return
      }
      if (archive) {
        seekArchive(seconds)
        setSeekHud({ label: playback.title || 'Архив', at: Date.now() })
        return
      }
      if (result === 'live-edge') {
        setSeekHud({ label: 'Прямой эфир', at: Date.now() })
        return
      }
      if (seconds < 0 && selectedChannel) {
        const from = Date.now() + seconds * 1000
        const ok = playProgram(selectedChannel, { start: from, end: from + 60 * 60 * 1000, title: 'Эфир со сдвигом' })
        setSeekHud({ label: ok ? 'Архив эфира' : 'Перемотка недоступна', at: Date.now() })
      }
    }

    const onSeek = (event) => applySeek(Number(event.detail?.seconds) || 0)
    window.addEventListener('mirefir:seek', onSeek)
    return () => window.removeEventListener('mirefir:seek', onSeek)
  }, [playProgram, playback, seekArchive, selectedChannel])

  useEffect(() => {
    if (!seekHud) return undefined
    const timer = window.setTimeout(() => setSeekHud(null), 1800)
    return () => window.clearTimeout(timer)
  }, [seekHud])

  useEffect(() => {
    if (!volumeTick) return undefined
    setShowVolume(true)
    const timer = window.setTimeout(() => setShowVolume(false), 1400)
    return () => window.clearTimeout(timer)
  }, [volumeTick])

  useEffect(() => {
    setRecordingActive(recorder.active)
  }, [recorder.active, setRecordingActive])

  useEffect(() => {
    if (!recordPulse || recordPulse === lastPulse.current) return
    lastPulse.current = recordPulse
    toggleRecord()
  }, [recordPulse])

  useEffect(() => {
    const video = videoRef.current
    if (!video) return undefined
    const onEnter = () => setPipOn(true)
    const onLeave = () => setPipOn(false)
    video.addEventListener('enterpictureinpicture', onEnter)
    video.addEventListener('leavepictureinpicture', onLeave)
    return () => {
      video.removeEventListener('enterpictureinpicture', onEnter)
      video.removeEventListener('leavepictureinpicture', onLeave)
    }
  }, [])

  async function togglePip() {
    const video = videoRef.current
    if (!video || !selectedChannel) return
    try {
      if (document.pictureInPictureElement) {
        await document.exitPictureInPicture()
        return
      }
      if (!document.pictureInPictureEnabled || video.disablePictureInPicture) {
        setRecHint('Картинка в картинке недоступна на этом устройстве')
        return
      }
      await video.requestPictureInPicture()
    } catch (err) {
      setRecHint(err.message || 'Не удалось свернуть в окошко')
    }
  }

  useEffect(() => {
    if (!pipPulse || pipPulse === lastPip.current) return
    lastPip.current = pipPulse
    togglePip()
  }, [pipPulse])

  async function toggleRecord() {
    setRecHint('')
    try {
      if (recorder.active) recorder.stop()
      else await recorder.start()
    } catch (err) {
      setRecHint(err.message || 'Не удалось начать запись')
    }
  }

  const level = muted ? 0 : volume
  const showSpinner = Boolean(selectedChannel) && loading && !error
  const goLive = () => {
    if (selectedChannel) selectChannel(selectedChannel.id)
  }

  const padItems = [
    { id: 'back', label: '−30с', run: () => window.dispatchEvent(new CustomEvent('mirefir:seek', { detail: { seconds: -30 } })) },
    { id: 'fwd', label: '+30с', run: () => window.dispatchEvent(new CustomEvent('mirefir:seek', { detail: { seconds: 30 } })) },
    { id: 'live', label: 'Прямой эфир', run: goLive },
    { id: 'guide', label: 'Гид', run: toggleLiveGuide },
    { id: 'rec', label: recorder.active ? 'Стоп' : 'REC', run: toggleRecord },
    { id: 'pip', label: 'PiP', run: togglePip },
    { id: 'voice', label: 'Голос', run: requestVoiceSearch },
    { id: 'menu', label: 'Меню', run: openMenu },
  ]
  padActionsRef.current = padItems
  const elapsedLabel = hasBounds ? formatHms(displayMs - boundsStart) : '00:00:00'
  const totalLabel = hasBounds ? formatHms(durationMs) : '00:00:00'

  return (
    <section
      className={
        fullscreen
          ? 'fixed inset-0 z-40 bg-black'
          : `relative flex min-w-[360px] flex-[1.15] flex-col bg-black ${focusZone === 'player' ? 'ring-1 ring-accent/50' : ''}`
      }
      onClick={() => {
        setFocusZone('player')
        showPad()
      }}
      onPointerMove={showPad}
      onDoubleClick={() => selectedChannel && setIsFullscreen((value) => !value)}
    >
      <video
        ref={videoRef}
        className="h-full w-full bg-black object-contain"
        controls={false}
        playsInline
        autoPlay
        disablePictureInPicture={false}
      />
      <ClockOverlay />

      {!selectedChannel ? (
        <div className="absolute inset-0 flex items-center justify-center text-white/40">Выберите канал</div>
      ) : null}

      {showSpinner ? (
        <div className="absolute inset-0 z-10 flex flex-col items-center justify-center gap-3 bg-black/35">
          <div className="player-spinner" />
          <div className="text-xs tracking-wide text-white/60">Загрузка потока…</div>
        </div>
      ) : null}

      {error ? (
        <div className="absolute inset-0 z-10 flex items-center justify-center bg-black/70 px-8 text-center text-sm text-white/70">
          {error}
        </div>
      ) : null}

      {showVolume ? (
        <div className="absolute right-5 top-1/2 z-20 flex -translate-y-1/2 flex-col items-center gap-2 rounded-2xl bg-black/65 px-3 py-4">
          <div className="text-[11px] uppercase tracking-wider text-white/50">{muted || level === 0 ? 'Mute' : 'Громкость'}</div>
          <div className="flex h-28 w-1.5 items-end overflow-hidden rounded-full bg-white/15">
            <div className="w-full rounded-full bg-accent" style={{ height: `${Math.round(level * 100)}%` }} />
          </div>
          <div className="text-sm font-medium tabular-nums">{muted || level === 0 ? 'M' : `${Math.round(level * 100)}%`}</div>
        </div>
      ) : null}

      {liveGuideOpen ? null : (
      <div className="pointer-events-none absolute inset-x-0 top-0 bg-gradient-to-b from-black/70 to-transparent p-4">
        <div className="flex items-start justify-between">
          <div className="flex items-center gap-3">
            <LogoMark name={selectedChannel?.name || 'TV'} logo={selectedChannel?.logo} size={44} />
            <div>
              <div className="flex items-center gap-2 text-[13px] text-white/70">
                <span className="inline-flex items-center gap-1 text-live">
                  <span className="live-dot h-1.5 w-1.5 rounded-full bg-live" />
                  {playback?.mode === 'archive' ? 'АРХИВ' : 'LIVE'}
                </span>
                <span>{selectedChannel?.number}</span>
                {recorder.active ? (
                  <span className="inline-flex items-center gap-1 text-red-400">
                    <span className="live-dot h-1.5 w-1.5 rounded-full bg-red-500" />
                    REC
                  </span>
                ) : null}
              </div>
              <div className="text-lg font-semibold">{selectedChannel?.displayName}</div>
            </div>
          </div>
          <div className="flex items-center gap-3">
            {selectedChannel ? (
              <>
                <button
                  type="button"
                  className={`pointer-events-auto rounded-lg px-3 py-1.5 text-xs font-semibold ${
                    pipOn ? 'bg-accent' : 'bg-white/15 hover:bg-white/25'
                  }`}
                  onClick={(event) => {
                    event.stopPropagation()
                    togglePip()
                  }}
                >
                  PiP
                </button>
                <button
                  type="button"
                  className={`pointer-events-auto rounded-lg px-3 py-1.5 text-xs font-semibold ${
                    recorder.active ? 'bg-red-600' : 'bg-white/15 hover:bg-white/25'
                  }`}
                  onClick={(event) => {
                    event.stopPropagation()
                    toggleRecord()
                  }}
                >
                  {recorder.active ? 'Стоп' : 'REC'}
                </button>
              </>
            ) : null}
          </div>
        </div>
      </div>
      )}

      {seekHud ? (
        <div className="absolute bottom-28 left-1/2 z-30 -translate-x-1/2 rounded-xl bg-black/70 px-4 py-2 text-sm">
          {seekHud.label}
        </div>
      ) : null}

      {liveGuideOpen || !padOn ? null : (
        <div
          className={`absolute bottom-28 left-1/2 z-20 flex -translate-x-1/2 gap-2 rounded-2xl p-2 ${
            padFocus === 'buttons' ? 'bg-black/70 ring-1 ring-accent/80' : 'bg-black/55'
          }`}
        >
          {padItems.map((item, index) => (
            <button
              key={item.id}
              type="button"
              className={`remote-hit min-w-[68px] rounded-xl px-2.5 text-sm font-medium ${
                padFocus === 'buttons' && index === buttonCursor ? 'bg-accent' : 'bg-white/12 hover:bg-accent'
              }`}
              onMouseEnter={() => {
                setPadFocus('buttons')
                setButtonCursor(index)
                showPad()
              }}
              onClick={(event) => {
                event.stopPropagation()
                if (item.id === 'back' || item.id === 'fwd') return
                item.run()
              }}
              onPointerDown={(event) => {
                if (item.id !== 'back' && item.id !== 'fwd') return
                event.preventDefault()
                event.stopPropagation()
                item.run()
                const timer = window.setInterval(() => item.run(), 220)
                const stop = () => {
                  window.clearInterval(timer)
                  window.removeEventListener('pointerup', stop)
                  window.removeEventListener('pointercancel', stop)
                }
                window.addEventListener('pointerup', stop)
                window.addEventListener('pointercancel', stop)
              }}
            >
              {item.label}
            </button>
          ))}
        </div>
      )}

      {liveGuideOpen ? null : (
      <div className="pointer-events-none absolute inset-x-0 bottom-0 bg-gradient-to-t from-black/80 to-transparent p-4">
        <div className="text-sm text-white/90">{program?.title || playback?.title || 'Прямой эфир'}</div>
        <div className="text-xs text-white/50">
          {hasBounds ? formatRange(boundsStart, boundsEnd) : selectedChannel?.group}
          {nextProgram && !archive ? `  ·  далее ${nextProgram.title}` : ''}
        </div>
        {padOn && hasBounds ? (
          <div className="pointer-events-auto mt-2">
            <div className="mb-1 flex justify-end">
              <span className="text-[13px] font-medium tabular-nums text-white/90">
                {elapsedLabel}/{totalLabel}
              </span>
            </div>
            <div
              ref={barRef}
              className={`relative h-2.5 cursor-pointer rounded-full bg-white/15 ${
                padFocus === 'bar' ? 'ring-2 ring-white/80' : ''
              }`}
              onPointerDown={(event) => {
                event.preventDefault()
                event.stopPropagation()
                event.currentTarget.setPointerCapture(event.pointerId)
                startBarDrag(event.clientX)
              }}
              onPointerMove={(event) => moveBarDrag(event.clientX)}
              onPointerUp={endBarDrag}
              onPointerCancel={endBarDrag}
              onMouseEnter={() => {
                setPadFocus('bar')
                showPad()
              }}
            >
              <div
                className="absolute inset-y-0 left-0 rounded-full bg-accent"
                style={{ width: `${displayProgress * 100}%` }}
              />
              <div
                className="absolute top-1/2 z-10 h-4 w-4 -translate-x-1/2 -translate-y-1/2 rounded-full border-2 border-white bg-accent shadow-md"
                style={{ left: `${displayProgress * 100}%` }}
              />
            </div>
          </div>
        ) : (
          <div className="mt-2 h-1 overflow-hidden rounded-full bg-white/15">
            <div className="h-full rounded-full bg-accent" style={{ width: `${progress * 100}%` }} />
          </div>
        )}
        {recHint || recorder.error ? (
          <div className="mt-2 text-[11px] text-red-300">{recHint || recorder.error}</div>
        ) : null}
        <div className="mt-2 text-[11px] text-white/35">
          {fullscreen || focusZone === 'player'
            ? 'OK — панель · ↓ меню · ← гид · ←→ перемотка на линии · Назад — закрыть'
            : 'Enter — на весь экран · ← гид · P — PiP · V — голос'}
        </div>
      </div>
      )}

      {recorder.askWhere ? (
        <div
          className="absolute inset-0 z-30 flex items-center justify-center bg-black/55 p-4"
          onClick={(event) => {
            event.stopPropagation()
            recorder.cancelAsk()
          }}
        >
          <div
            className="w-full max-w-sm rounded-2xl border border-white/10 bg-[#10151e] p-5"
            onClick={(event) => event.stopPropagation()}
          >
            <div className="mb-1 text-lg font-semibold">Куда записать?</div>
            <p className="mb-4 text-sm text-white/50">
              Если места на устройстве хватает — можно во внутреннюю память. Иначе выберите диск или флешку.
            </p>
            <div className="flex flex-col gap-2">
              <button
                type="button"
                className="rounded-xl bg-accent px-4 py-2.5 text-sm"
                onClick={() => recorder.pickInternal().catch((err) => setRecHint(err.message))}
              >
                Внутренняя память
              </button>
              <button
                type="button"
                className="rounded-xl bg-white/10 px-4 py-2.5 text-sm"
                onClick={() => recorder.pickExternal().catch((err) => setRecHint(err.message))}
              >
                Диск / флешка
              </button>
              <button type="button" className="text-sm text-white/40" onClick={recorder.cancelAsk}>
                Отмена
              </button>
            </div>
          </div>
        </div>
      ) : null}
    </section>
  )
}
