import { useEffect, useMemo, useRef, useState } from 'react'
import { formatRange, getProgramProgress } from '../lib/epg.js'
import { useClock } from '../hooks/useClock.js'
import { usePlayer } from '../store/PlayerContext.jsx'
import { LogoMark } from './LogoMark.jsx'

const ROW = 80
const OVERSCAN = 8

export function ChannelActionMenu() {
  const { channelMenu, channelMenuItems, runChannelMenuItem, selectedChannel, channels, liveGuideOpen } = usePlayer()
  if (!channelMenu || liveGuideOpen) return null
  const channel = channels.find((item) => item.id === channelMenu.channelId) || selectedChannel
  const items = channelMenuItems(channel)
  if (!items.length) return null

  return (
    <div className="pointer-events-none absolute inset-0 z-40">
      <div className="pointer-events-auto absolute top-24 left-6 min-w-[240px] rounded-xl border border-white/15 bg-[#10151e] p-2 shadow-2xl">
        <div className="px-3 py-1 text-[12px] text-white/40">{channel?.displayName || channel?.name}</div>
        {items.map((item, index) => (
          <button
            key={item.id}
            type="button"
            className={`remote-hit mb-0.5 w-full rounded-lg px-3 py-2 text-left text-[14px] ${
              index === channelMenu.cursor ? 'bg-accent/80' : 'hover:bg-white/8'
            }`}
            onClick={() => runChannelMenuItem(channel, item.id)}
          >
            {item.title}
          </button>
        ))}
      </div>
    </div>
  )
}

export function ChannelList() {
  const now = useClock(15000)
  const {
    visibleChannels,
    selectedChannel,
    selectChannel,
    focusZone,
    setFocusZone,
    favorites,
    getCurrentProgram,
    getNextProgram,
    listMode,
    selectedGroupId,
    settings,
    movingFavoriteId,
    openChannelMenu,
  } = usePlayer()

  const titles = {
    all: 'Все каналы',
    favorites: 'Избранное',
    recent: 'Недавние',
    movies: 'Фильмы',
    series: 'Сериалы',
    archive: `Архив · ${settings.archiveDays} дн.`,
  }
  const heading = titles[listMode] || titles[selectedGroupId] || 'Каналы'
  const listRef = useRef(null)
  const holdTimer = useRef(0)
  const [scrollTop, setScrollTop] = useState(0)
  const [viewport, setViewport] = useState(640)

  useEffect(() => {
    const el = listRef.current
    if (!el) return undefined
    const measure = () => setViewport(el.clientHeight || 640)
    measure()
    const observer = new ResizeObserver(measure)
    observer.observe(el)
    return () => observer.disconnect()
  }, [])

  useEffect(() => {
    const el = listRef.current
    if (!el || !selectedChannel) return
    const index = visibleChannels.findIndex((channel) => channel.id === selectedChannel.id)
    if (index < 0) return
    const top = index * ROW
    if (top < el.scrollTop) el.scrollTop = top
    else if (top + ROW > el.scrollTop + el.clientHeight) el.scrollTop = top + ROW - el.clientHeight
  }, [focusZone, movingFavoriteId, selectedChannel, visibleChannels])

  const { start, end } = useMemo(() => {
    const first = Math.max(0, Math.floor(scrollTop / ROW) - OVERSCAN)
    const last = Math.min(visibleChannels.length, Math.ceil((scrollTop + viewport) / ROW) + OVERSCAN)
    return { start: first, end: last }
  }, [scrollTop, viewport, visibleChannels.length])

  const slice = visibleChannels.slice(start, end)

  const armHold = (channel) => {
    window.clearTimeout(holdTimer.current)
    holdTimer.current = window.setTimeout(() => {
      selectChannel(channel.id)
      openChannelMenu(channel, 0)
    }, 550)
  }
  const cancelHold = () => window.clearTimeout(holdTimer.current)

  return (
    <section
      className={`relative flex min-w-0 flex-1 flex-col border-r border-line bg-[#080b11] ${
        focusZone === 'channels' ? 'ring-1 ring-accent/40' : ''
      }`}
      onClick={() => setFocusZone('channels')}
    >
      <div className="flex items-center justify-between px-4 py-3">
        <div className="text-[11px] font-semibold uppercase tracking-[0.18em] text-white/35">{heading}</div>
        <div className="text-[11px] text-white/30">{visibleChannels.length}</div>
      </div>
      <div
        ref={listRef}
        className="scroll-thin flex-1 overflow-y-auto px-2 pb-3"
        onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
      >
        {visibleChannels.length === 0 ? (
          <div className="px-4 py-10 text-sm text-white/40">В этой категории пока нет каналов</div>
        ) : (
          <div style={{ height: visibleChannels.length * ROW, position: 'relative' }}>
            {slice.map((channel, offset) => {
              const index = start + offset
              const active = channel.id === selectedChannel?.id
              const focused = focusZone === 'channels' && active
              const program = getCurrentProgram(channel)
              const next = getNextProgram(channel)
              const progress = getProgramProgress(program, now.getTime())
              const fav = favorites.includes(channel.id)
              const moving = movingFavoriteId === channel.id

              return (
                <button
                  key={channel.id}
                  type="button"
                  data-active={active}
                  onClick={() => selectChannel(channel.id)}
                  onContextMenu={(event) => {
                    event.preventDefault()
                    selectChannel(channel.id)
                    openChannelMenu(channel, 0)
                  }}
                  onPointerDown={() => armHold(channel)}
                  onPointerUp={cancelHold}
                  onPointerLeave={cancelHold}
                  onPointerCancel={cancelHold}
                  style={{ top: index * ROW, height: ROW - 4 }}
                  className={`remote-hit absolute right-0 left-0 flex items-center gap-3 rounded-xl px-2.5 text-left transition ${
                    moving
                      ? 'fav-moving bg-accent/35'
                      : focused
                        ? 'focus-tile bg-accent/20'
                        : active
                          ? 'bg-white/7'
                          : 'hover:bg-white/4'
                  }`}
                >
                  {settings.showChannelNumbers ? (
                    <div className="w-8 text-right text-[12px] tabular-nums text-white/35">{channel.number}</div>
                  ) : null}
                  <LogoMark name={channel.name} logo={channel.logo} size={40} />
                  <div className="min-w-0 flex-1">
                    <div className="flex items-center gap-2">
                      <div className="truncate text-[14px] font-medium">{channel.displayName || channel.name}</div>
                      {fav ? <span className="text-[11px] text-amber-300">★</span> : null}
                    </div>
                    <div className="truncate text-[12px] text-white/70">
                      {program ? program.title : 'Нет в программе'}
                    </div>
                    <div className="truncate text-[11px] text-white/35">
                      {next ? `Далее: ${next.title}` : program ? formatRange(program.start, program.end) : ''}
                    </div>
                    {settings.showEpgProgress ? (
                      <div className="mt-1.5 h-1 overflow-hidden rounded-full bg-white/10">
                        <div className="h-full rounded-full bg-accent" style={{ width: `${progress * 100}%` }} />
                      </div>
                    ) : null}
                  </div>
                </button>
              )
            })}
          </div>
        )}
      </div>
    </section>
  )
}
