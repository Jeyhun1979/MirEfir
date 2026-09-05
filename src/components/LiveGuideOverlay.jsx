import { useEffect, useMemo, useRef, useState } from 'react'
import { formatRange, formatRemaining, getProgramProgress } from '../lib/epg.js'
import { arrowDir, isBackKey, isOkKey } from '../lib/remoteKeys.js'
import { useClock } from '../hooks/useClock.js'
import { usePlayer } from '../store/PlayerContext.jsx'
import { LogoMark } from './LogoMark.jsx'

const ROW = 76
const OVERSCAN = 6

export function LiveGuideOverlay() {
  const now = useClock(15000)
  const {
    liveGuideOpen,
    setLiveGuideOpen,
    channels,
    selectedChannel,
    selectChannel,
    getCurrentProgram,
    getNextProgram,
    settings,
    favorites,
  } = usePlayer()

  const list = useMemo(
    () => channels.filter((channel) => !(settings.hiddenGroups || []).includes(channel.group)),
    [channels, settings.hiddenGroups],
  )

  const [cursor, setCursor] = useState(0)
  const [scrollTop, setScrollTop] = useState(0)
  const [viewport, setViewport] = useState(640)
  const scrollerRef = useRef(null)

  useEffect(() => {
    if (!liveGuideOpen) return
    const index = Math.max(
      0,
      list.findIndex((channel) => channel.id === selectedChannel?.id),
    )
    setCursor(index)
    const el = scrollerRef.current
    if (el) {
      const top = Math.max(0, index * ROW - el.clientHeight / 2 + ROW)
      el.scrollTop = top
      setScrollTop(top)
    }
  }, [list, liveGuideOpen, selectedChannel?.id])

  useEffect(() => {
    const el = scrollerRef.current
    if (!el) return undefined
    const measure = () => setViewport(el.clientHeight || 640)
    measure()
    const observer = new ResizeObserver(measure)
    observer.observe(el)
    return () => observer.disconnect()
  }, [liveGuideOpen])

  useEffect(() => {
    if (!liveGuideOpen) return undefined

    const onKey = (event) => {
      const dir = arrowDir(event)
      const handled = dir || isOkKey(event) || isBackKey(event) || event.key === settings.keys?.liveGuide
      if (!handled) return
      event.preventDefault()
      event.stopPropagation()

      if (isBackKey(event) || dir === 'left' || event.key === settings.keys?.liveGuide) {
        setLiveGuideOpen(false)
        return
      }
      if (dir === 'up' || dir === 'down') {
        const step = dir === 'down' ? 1 : -1
        setCursor((current) => {
          const next = (current + step + list.length) % list.length
          const el = scrollerRef.current
          if (el) {
            const top = next * ROW
            if (top < el.scrollTop) el.scrollTop = top
            else if (top + ROW > el.scrollTop + el.clientHeight) el.scrollTop = top + ROW - el.clientHeight
          }
          return next
        })
        return
      }
      if (isOkKey(event)) {
        const channel = list[cursor]
        if (channel) selectChannel(channel.id)
      }
    }

    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [cursor, list, liveGuideOpen, selectChannel, setLiveGuideOpen, settings.keys?.liveGuide])

  if (!liveGuideOpen) return null

  const start = Math.max(0, Math.floor(scrollTop / ROW) - OVERSCAN)
  const end = Math.min(list.length, Math.ceil((scrollTop + viewport) / ROW) + OVERSCAN)
  const slice = list.slice(start, end)
  const focused = list[cursor]
  const focusedProgram = getCurrentProgram(focused)
  const focusedNext = getNextProgram(focused)

  return (
    <div className="fixed inset-0 z-[45]" onClick={() => setLiveGuideOpen(false)}>
      <aside
        className="absolute inset-y-0 left-0 flex w-[min(440px,42vw)] flex-col bg-gradient-to-r from-black/70 via-black/45 to-transparent"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="flex items-center justify-between px-4 py-3">
          <div>
            <div className="text-[11px] font-semibold uppercase tracking-[0.18em] text-white/40">Телепрограмма</div>
            <div className="text-sm text-white/70">{list.length} каналов · эфир не прерывается</div>
          </div>
          <button type="button" className="rounded-lg bg-white/10 px-2 py-1 text-xs" onClick={() => setLiveGuideOpen(false)}>
            Закрыть
          </button>
        </div>

        <div
          ref={scrollerRef}
          className="scroll-thin min-h-0 flex-1 overflow-y-auto px-2"
          onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
        >
          <div style={{ height: list.length * ROW, position: 'relative' }}>
            {slice.map((channel, offset) => {
              const index = start + offset
              const active = channel.id === selectedChannel?.id
              const hovered = index === cursor
              const program = getCurrentProgram(channel)
              const progress = getProgramProgress(program, now.getTime())
              const fav = favorites.includes(channel.id)

              return (
                <button
                  key={channel.id}
                  type="button"
                  style={{ top: index * ROW, height: ROW - 4 }}
                  onClick={() => {
                    setCursor(index)
                    selectChannel(channel.id)
                  }}
                  className={`remote-hit absolute right-0 left-0 flex items-center gap-3 rounded-xl px-2.5 text-left ${
                    hovered ? 'bg-accent/35 ring-1 ring-accent/70' : active ? 'bg-white/10' : 'hover:bg-white/6'
                  }`}
                >
                  <div className="w-8 text-right text-[12px] tabular-nums text-white/35">{channel.number}</div>
                  <LogoMark name={channel.name} logo={channel.logo} size={40} />
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-2">
                      <div className="truncate text-[14px] font-medium">{channel.displayName}</div>
                      {fav ? <span className="text-[11px] text-amber-300">★</span> : null}
                    </div>
                    <div className="truncate text-[12px] text-white/75">{program?.title || 'Прямой эфир'}</div>
                    <div className="mt-1 h-1 overflow-hidden rounded-full bg-white/10">
                      <div className="h-full rounded-full bg-accent" style={{ width: `${progress * 100}%` }} />
                    </div>
                  </div>
                </button>
              )
            })}
          </div>
        </div>

        <div className="border-t border-white/10 bg-black/20 px-4 py-3">
          <div className="text-sm font-medium">{focusedProgram?.title || 'Прямой эфир'}</div>
          <div className="text-xs text-white/45">
            {focusedProgram ? `${formatRange(focusedProgram.start, focusedProgram.end)} · ещё ${formatRemaining(focusedProgram, now.getTime())}` : focused?.group}
            {focusedNext ? `  ·  далее ${focusedNext.title}` : ''}
          </div>
          <div className="mt-2 text-[11px] text-white/35">↑↓ листать · OK выбрать · Назад закрыть · гиромышь: клик</div>
        </div>
      </aside>
    </div>
  )
}
