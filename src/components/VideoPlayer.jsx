import { useEffect, useRef, useState } from 'react'
import { formatRange, getProgramProgress } from '../lib/epg.js'
import { useClock } from '../hooks/useClock.js'
import { useHls } from '../hooks/useHls.js'
import { useRecorder } from '../hooks/useRecorder.js'
import { usePlayer } from '../store/PlayerContext.jsx'
import { ClockOverlay } from './ClockOverlay.jsx'
import { LogoMark } from './LogoMark.jsx'

export function VideoPlayer({ fullscreen = false }) {
  const videoRef = useRef(null)
  const now = useClock(1000)
  const {
    selectedChannel,
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
  const progress = getProgramProgress(program, now.getTime())
  const pauseForMulti = uiScreen === 'multiview' && !recordingActive
  const { error, loading } = useHls(videoRef, pauseForMulti ? '' : selectedChannel?.url || '')
  const recorder = useRecorder(videoRef, selectedChannel, settings, updateSettings)
  const [showVolume, setShowVolume] = useState(false)
  const [recHint, setRecHint] = useState('')
  const [pipOn, setPipOn] = useState(false)
  const [padOn, setPadOn] = useState(false)
  const lastPulse = useRef(0)
  const lastPip = useRef(0)
  const padTimer = useRef(0)

  const showPad = () => {
    setPadOn(true)
    window.clearTimeout(padTimer.current)
    padTimer.current = window.setTimeout(() => setPadOn(false), 4500)
  }

  useEffect(() => {
    const video = videoRef.current
    if (!video) return
    video.volume = volume
    video.muted = muted
  }, [volume, muted])

  useEffect(() => {
    const onPad = () => showPad()
    window.addEventListener('mirefir:pad', onPad)
    return () => window.removeEventListener('mirefir:pad', onPad)
  }, [])

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
      {liveGuideOpen ? null : <ClockOverlay />}

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
                  LIVE
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
            {fullscreen ? (
              <div className="text-xl font-medium tabular-nums">
                {now.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}
              </div>
            ) : null}
          </div>
        </div>
      </div>
      )}

      {liveGuideOpen || !padOn ? null : (
        <div className="absolute bottom-24 left-1/2 z-20 flex -translate-x-1/2 gap-2 rounded-2xl bg-black/55 p-2">
          {[
            { id: 'guide', label: 'Гид', run: toggleLiveGuide },
            { id: 'rec', label: recorder.active ? 'Стоп' : 'REC', run: toggleRecord },
            { id: 'pip', label: 'PiP', run: togglePip },
            { id: 'voice', label: 'Голос', run: requestVoiceSearch },
            { id: 'menu', label: 'Меню', run: openMenu },
          ].map((item) => (
            <button
              key={item.id}
              type="button"
              className="remote-hit min-w-[76px] rounded-xl bg-white/12 px-3 text-sm font-medium hover:bg-accent"
              onClick={(event) => {
                event.stopPropagation()
                item.run()
              }}
            >
              {item.label}
            </button>
          ))}
        </div>
      )}

      {liveGuideOpen ? null : (
      <div className="pointer-events-none absolute inset-x-0 bottom-0 bg-gradient-to-t from-black/80 to-transparent p-4">
        <div className="text-sm text-white/90">{program?.title || 'Прямой эфир'}</div>
        <div className="text-xs text-white/50">
          {program ? formatRange(program.start, program.end) : selectedChannel?.group}
          {nextProgram ? `  ·  далее ${nextProgram.title}` : ''}
        </div>
        <div className="mt-2 h-1 overflow-hidden rounded-full bg-white/15">
          <div className="h-full rounded-full bg-accent" style={{ width: `${progress * 100}%` }} />
        </div>
        {recHint || recorder.error ? (
          <div className="mt-2 text-[11px] text-red-300">{recHint || recorder.error}</div>
        ) : null}
        <div className="mt-2 text-[11px] text-white/35">
          {fullscreen || focusZone === 'player'
            ? '← телепрограмма · P — окошко · R — запись · V — голос · ↑↓ канал'
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
