import { useEffect, useMemo, useRef, useState } from 'react'
import {
  collectGuideDays,
  formatDayLong,
  formatDayParts,
  formatRange,
  formatRemaining,
  getProgramProgress,
  programsOnDay,
  startOfDay,
} from '../lib/epg.js'
import { programHasArchive } from '../lib/catchup.js'
import { arrowDir, isBackKey, isConfirmKey, isMenuKey } from '../lib/remoteKeys.js'
import { useClock } from '../hooks/useClock.js'
import { usePlayer } from '../store/PlayerContext.jsx'
import { LogoMark } from './LogoMark.jsx'

const CHANNEL_ROW = 72
const OVERSCAN = 6

function wrapIndex(index, length) {
  if (!length) return 0
  return (index + length) % length
}

function centerChild(container, child) {
  if (!container || !child) return
  const box = container.getBoundingClientRect()
  const row = child.getBoundingClientRect()
  const delta = row.top + row.height / 2 - (box.top + box.height / 2)
  container.scrollTop = Math.max(0, container.scrollTop + delta)
}

function MarqueeText({ text, active }) {
  const wrapRef = useRef(null)
  const textRef = useRef(null)
  const [overflow, setOverflow] = useState(false)

  useEffect(() => {
    const wrap = wrapRef.current
    const node = textRef.current
    if (!wrap || !node) return
    setOverflow(node.scrollWidth > wrap.clientWidth + 2)
  }, [text, active])

  return (
    <div ref={wrapRef} className="min-w-0 flex-1 overflow-hidden">
      <span
        ref={textRef}
        className={active && overflow ? 'epg-marquee inline-block whitespace-nowrap' : 'block truncate'}
      >
        {text}
      </span>
    </div>
  )
}

function DayButton({ day, hovered, selected, onClick, onEnter }) {
  const parts = formatDayParts(day)
  return (
    <button
      type="button"
      onClick={onClick}
      onMouseEnter={onEnter}
      className={`remote-hit mb-1 flex w-full flex-col items-center rounded-md px-1 py-1.5 ${
        hovered ? 'bg-accent/75' : selected ? 'text-sky-300' : 'text-white/70 hover:bg-white/8'
      }`}
    >
      <span className="text-[18px] leading-5 font-semibold">{parts.day}</span>
      <span className="text-[11px] leading-4 text-white/50">{parts.month}</span>
      <span className="text-[16px] leading-5 font-medium">{parts.week}</span>
    </button>
  )
}

function ArchiveMark({ visible }) {
  if (!visible) return null
  return (
    <span className="ml-auto shrink-0 rounded-full border border-white/25 px-1.5 text-[10px] leading-4 text-white/55" title="Архив">
      ↺
    </span>
  )
}

function DetailCard({ program, now, focused, cardRef }) {
  if (!program) {
    return (
      <div
        ref={cardRef}
        className={`scroll-thin max-h-[min(72vh,560px)] w-[min(340px,30vw)] overflow-y-auto rounded-2xl bg-black/55 px-4 py-3 backdrop-blur-[2px] ${
          focused ? 'ring-2 ring-white' : ''
        }`}
      >
        <div className="text-[16px] text-white/70">Нет описания</div>
      </div>
    )
  }

  const progress = getProgramProgress(program, now.getTime())
  const remaining = formatRemaining(program, now.getTime())
  const live = program.start <= now.getTime() && now.getTime() < program.end

  return (
    <div
      ref={cardRef}
      className={`scroll-thin max-h-[min(72vh,560px)] w-[min(340px,30vw)] overflow-y-auto rounded-2xl bg-black/55 px-4 py-3 backdrop-blur-[2px] ${
        focused ? 'ring-2 ring-white' : ''
      }`}
    >
      <div className="text-[18px] font-semibold leading-tight">{program.title}</div>
      <div className="mt-1.5 flex items-center gap-2 text-[12px] text-white/70">
        <span>{formatRange(program.start, program.end)}</span>
        {live ? <span className="shrink-0 text-white/55">{remaining}</span> : null}
      </div>
      {live ? (
        <div className="mt-2 h-[3px] overflow-hidden rounded-full bg-white/20">
          <span className="block h-full bg-white" style={{ width: `${progress * 100}%` }} />
        </div>
      ) : null}
      {program.description ? (
        <div className="mt-2 text-[13px] leading-relaxed text-white/75">{program.description}</div>
      ) : (
        <div className="mt-2 text-[13px] text-white/45">Нет описания</div>
      )}
    </div>
  )
}

export function LiveGuideOverlay() {
  const now = useClock(15000)
  const {
    liveGuideView,
    setLiveGuideView,
    setLiveGuideOpen,
    openMenu,
    channels,
    groups,
    recentIds,
    selectedChannel,
    selectedGroupId,
    selectChannel,
    selectGroup,
    playProgram,
    channelAllowsArchive,
    getCurrentProgram,
    getPrograms,
    settings,
    favorites,
    toggleFavorite,
    channelMenu,
    setChannelMenu,
    openChannelMenu,
    movingFavoriteId,
    startFavoriteMove,
    commitFavoriteMove,
    moveFavorite,
    goBack,
    error,
  } = usePlayer()

  const [groupId, setGroupId] = useState(selectedGroupId || 'all')
  const [channelCursor, setChannelCursor] = useState(0)
  const [programCursor, setProgramCursor] = useState(0)
  const [groupCursor, setGroupCursor] = useState(0)
  const [dayCursor, setDayCursor] = useState(0)
  const [daysOpen, setDaysOpen] = useState(false)
  const [focusCol, setFocusCol] = useState('channels')
  const [scrollTop, setScrollTop] = useState(0)
  const [viewport, setViewport] = useState(640)
  const channelRef = useRef(null)
  const programRef = useRef(null)
  const dayRef = useRef(null)
  const detailRef = useRef(null)
  const openedRef = useRef(false)
  const okTimer = useRef(0)
  const okHeld = useRef(false)
  const pendingAlignRef = useRef('live')

  const list = useMemo(() => {
    const hidden = settings.hiddenGroups || []
    let next = channels.filter((channel) => !hidden.includes(channel.group))
    if (groupId === 'favorites') next = favorites.map((id) => channels.find((channel) => channel.id === id)).filter(Boolean)
    else if (groupId === 'recent') next = (recentIds || []).map((id) => channels.find((channel) => channel.id === id)).filter(Boolean)
    else if (groupId && groupId !== 'all') next = next.filter((channel) => channel.group === groupId)
    return next
  }, [channels, favorites, groupId, recentIds, settings.hiddenGroups])

  const activeGroup = groups.find((group) => group.id === groupId) || groups[0]
  const focusedChannel = list[channelCursor] || selectedChannel
  const allPrograms = useMemo(() => getPrograms(focusedChannel), [focusedChannel, getPrograms])
  const todayStamp = startOfDay(now.getTime())
  const days = useMemo(
    () =>
      collectGuideDays({
        programs: allPrograms,
        now: todayStamp,
        archiveDays: settings.archiveDays,
        archiveEnabled: settings.archiveEnabled,
        catchupDays: focusedChannel?.catchupDays,
        epgDays: settings.epgDays,
      }),
    [allPrograms, focusedChannel?.catchupDays, settings.archiveDays, settings.archiveEnabled, settings.epgDays, todayStamp],
  )
  const selectedDay = days[dayCursor] || startOfDay(now.getTime())
  const visiblePrograms = useMemo(() => programsOnDay(allPrograms, selectedDay), [allPrograms, selectedDay])
  const focusedProgram = visiblePrograms[programCursor] || getCurrentProgram(focusedChannel)
  const menuItems = (channel) => {
    if (!channel) return []
    const starred = favorites.includes(channel.id)
    const items = [{ id: 'fav', title: starred ? 'Удалить из избранного' : 'Добавить в избранное' }]
    if (groupId === 'favorites' && starred) items.push({ id: 'move', title: 'Переместить' })
    return items
  }

  useEffect(() => {
    if (!movingFavoriteId) return
    const index = list.findIndex((channel) => channel.id === movingFavoriteId)
    if (index >= 0) setChannelCursor(index)
  }, [list, movingFavoriteId])

  useEffect(() => {
    if (!liveGuideView) {
      openedRef.current = false
      return
    }
    if (openedRef.current) return
    openedRef.current = true
    const nextGroup = selectedGroupId || 'all'
    const hidden = settings.hiddenGroups || []
    let nextList = channels.filter((channel) => !hidden.includes(channel.group))
    if (nextGroup === 'favorites') nextList = favorites.map((id) => channels.find((channel) => channel.id === id)).filter(Boolean)
    else if (nextGroup === 'recent') nextList = (recentIds || []).map((id) => channels.find((channel) => channel.id === id)).filter(Boolean)
    else if (nextGroup !== 'all') nextList = nextList.filter((channel) => channel.group === nextGroup)
    setGroupId(nextGroup)
    setGroupCursor(Math.max(0, groups.findIndex((group) => group.id === nextGroup)))
    const index = Math.max(
      0,
      nextList.findIndex((channel) => channel.id === selectedChannel?.id),
    )
    setChannelCursor(index)
    setDaysOpen(Boolean(settings.archiveEnabled))
    setFocusCol(liveGuideView === 'groups' ? 'groups' : 'channels')
    pendingAlignRef.current = 'live'
    requestAnimationFrame(() => {
      const el = channelRef.current
      if (!el) return
      const top = Math.max(0, index * CHANNEL_ROW - el.clientHeight / 2 + CHANNEL_ROW)
      el.scrollTop = top
      setScrollTop(top)
    })
  }, [channels, favorites, groups, liveGuideView, recentIds, selectedChannel?.id, selectedGroupId, settings.archiveEnabled, settings.hiddenGroups])

  useEffect(() => {
    if (!visiblePrograms.length) {
      setProgramCursor(0)
      return
    }
    const current = getCurrentProgram(focusedChannel)
    const liveIndex = visiblePrograms.findIndex((item) => item.id === current?.id || (current && item.start === current.start))
    pendingAlignRef.current = 'live'
    setProgramCursor(selectedDay === todayStamp && liveIndex >= 0 ? liveIndex : 0)
  }, [focusedChannel?.id, liveGuideView, selectedDay, todayStamp, visiblePrograms.length])

  useEffect(() => {
    if (detailRef.current) detailRef.current.scrollTop = 0
  }, [focusedProgram?.id])

  useEffect(() => {
    if (!liveGuideView || !days.length) return
    const today = startOfDay(Date.now())
    const index = days.findIndex((day) => day === today)
    if (index >= 0) setDayCursor(index)
  }, [days.length, liveGuideView])

  useEffect(() => {
    if (!liveGuideView || !daysOpen) return
    const el = dayRef.current?.querySelector(`[data-day="${dayCursor}"]`)
    centerChild(dayRef.current, el)
  }, [dayCursor, daysOpen, days.length, liveGuideView])

  useEffect(() => {
    const el = channelRef.current
    if (!el) return undefined
    const measure = () => setViewport(el.clientHeight || 640)
    measure()
    const observer = new ResizeObserver(measure)
    observer.observe(el)
    return () => observer.disconnect()
  }, [liveGuideView])

  useEffect(() => {
    if (!liveGuideView) return undefined
    let tries = 0
    let frame = 0
    const run = () => {
      const list = programRef.current
      const node = list?.querySelector(`[data-prog="${programCursor}"]`)
      if (!list || !node) {
        if (tries < 12) {
          tries += 1
          frame = window.requestAnimationFrame(run)
        }
        return
      }
      const mode = pendingAlignRef.current
      if (mode === 'live' || mode === 'day') {
        const dayEl = dayRef.current?.querySelector(`[data-day="${dayCursor}"]`)
        const box = list.getBoundingClientRect()
        const targetMid = dayEl
          ? dayEl.getBoundingClientRect().top + dayEl.getBoundingClientRect().height / 2
          : box.top + box.height / 2
        const progMid = node.getBoundingClientRect().top + node.getBoundingClientRect().height / 2
        list.scrollTop = Math.max(0, list.scrollTop + (progMid - targetMid))
        pendingAlignRef.current = null
        return
      }
      node.scrollIntoView({ block: 'nearest' })
    }
    frame = window.requestAnimationFrame(run)
    return () => window.cancelAnimationFrame(frame)
  }, [channelCursor, dayCursor, daysOpen, focusedChannel?.id, liveGuideView, programCursor])

  useEffect(() => {
    if (!liveGuideView) return undefined

    const moveChannel = (step) => {
      setChannelCursor((current) => {
        const next = wrapIndex(current + step, list.length)
        const el = channelRef.current
        if (el) {
          const top = next * CHANNEL_ROW
          if (top < el.scrollTop) el.scrollTop = top
          else if (top + CHANNEL_ROW > el.scrollTop + el.clientHeight) el.scrollTop = top + CHANNEL_ROW - el.clientHeight
        }
        return next
      })
    }

    const tuneChannel = (channel) => {
      if (!channel) return
      selectChannel(channel.id)
    }

    const playFocused = () => {
      if (!focusedChannel || !focusedProgram) return
      const ok = playProgram(focusedChannel, focusedProgram)
      if (ok) setLiveGuideOpen(false)
    }

    const openFav = (channel) => {
      if (!channel) return
      okHeld.current = true
      openChannelMenu(channel, 0)
    }

    const runMenuItem = (channel, itemId) => {
      setChannelMenu(null)
      if (!channel || !itemId) return
      if (itemId === 'fav') toggleFavorite(channel.id)
      if (itemId === 'move') startFavoriteMove(channel.id)
    }

    const startOkHold = (channel) => {
      okHeld.current = false
      window.clearTimeout(okTimer.current)
      okTimer.current = window.setTimeout(() => openFav(channel), 550)
    }

    const onKeyUp = (event) => {
      if (!isConfirmKey(event)) return
      window.clearTimeout(okTimer.current)
      if (okHeld.current) return
      event.preventDefault()
      event.stopPropagation()
      if (movingFavoriteId) {
        commitFavoriteMove()
        return
      }
      if (channelMenu) {
        const channel = channels.find((item) => item.id === channelMenu.channelId) || focusedChannel
        const items = menuItems(channel)
        runMenuItem(channel, items[channelMenu.cursor]?.id)
        return
      }
      if (liveGuideView === 'schedule' && (focusCol === 'programs' || focusCol === 'days' || focusCol === 'detail')) {
        playFocused()
        return
      }
      if (focusCol === 'programs' || focusCol === 'days' || focusCol === 'detail') {
        playFocused()
        return
      }
      if (liveGuideView === 'categories' || liveGuideView === 'groups') {
        const inGroups = liveGuideView === 'groups' || focusCol === 'groups'
        if (inGroups) {
          const group = groups[groupCursor]
          if (group) {
            setGroupId(group.id)
            selectGroup(group.id)
            setLiveGuideView('categories')
            setFocusCol('channels')
            setChannelCursor(0)
          }
          return
        }
      }
      tuneChannel(focusedChannel)
    }

    const onKey = (event) => {
      const dir = arrowDir(event)
      const menuPressed = isMenuKey(event) || event.key === settings.keys?.menu
      if (!(dir || isConfirmKey(event) || isBackKey(event) || menuPressed || event.key === settings.keys?.liveGuide)) return
      event.preventDefault()
      event.stopPropagation()

      if (movingFavoriteId) {
        if (dir === 'up' || dir === 'down') {
          moveFavorite(movingFavoriteId, dir === 'down' ? 1 : -1)
          return
        }
        if (isConfirmKey(event)) return
        if (isBackKey(event) || menuPressed) {
          goBack()
        }
        return
      }

      if (channelMenu) {
        const channel = channels.find((item) => item.id === channelMenu.channelId) || focusedChannel
        const items = menuItems(channel)
        if (dir === 'up' || dir === 'down') {
          const step = dir === 'down' ? 1 : -1
          setChannelMenu((current) => ({
            ...current,
            cursor: wrapIndex((current?.cursor || 0) + step, items.length || 1),
          }))
          return
        }
        if (isBackKey(event) || dir === 'left' || menuPressed) {
          setChannelMenu(null)
          return
        }
        if (isConfirmKey(event) && !event.repeat) startOkHold(channel)
        return
      }

      if (menuPressed) {
        openFav(focusedChannel)
        return
      }

      if (isConfirmKey(event)) {
        if (focusCol === 'programs' || focusCol === 'days' || focusCol === 'detail') return
        if (!event.repeat) startOkHold(focusedChannel)
        return
      }

      if (isBackKey(event)) {
        if (liveGuideView === 'groups') {
          setLiveGuideView('categories')
          setFocusCol('channels')
        } else if (liveGuideView === 'channels') setLiveGuideView(null)
        else {
          setLiveGuideView('channels')
          setFocusCol('channels')
        }
        return
      }

      if (liveGuideView === 'channels' || liveGuideView === 'schedule') {
        if (dir === 'left' || event.key === settings.keys?.liveGuide) {
          if (focusCol === 'detail') {
            setFocusCol(daysOpen && days.length ? 'days' : 'programs')
            return
          }
          if (focusCol === 'days') {
            setFocusCol('programs')
            setDaysOpen(false)
            return
          }
          if (focusCol === 'programs') {
            setFocusCol('channels')
            setLiveGuideView('channels')
            setDaysOpen(false)
            return
          }
          setLiveGuideView('categories')
          setFocusCol('channels')
          return
        }
        if (dir === 'right') {
          if (focusCol === 'channels') {
            setFocusCol('programs')
            return
          }
          if (focusCol === 'programs' && days.length) {
            setDaysOpen(true)
            setFocusCol('days')
            return
          }
          if (focusCol === 'days' || focusCol === 'programs') {
            setFocusCol('detail')
          }
          return
        }
        if (dir === 'up' || dir === 'down') {
          const step = dir === 'down' ? 1 : -1
          if (focusCol === 'detail') {
            detailRef.current?.scrollBy({ top: step * 72, behavior: 'smooth' })
            return
          }
          if (focusCol === 'days') {
            pendingAlignRef.current = 'day'
            setDayCursor((current) => Math.min(days.length - 1, Math.max(0, current + step)))
          } else if (focusCol === 'programs') {
            pendingAlignRef.current = null
            setProgramCursor((current) => wrapIndex(current + step, visiblePrograms.length))
          } else moveChannel(step)
        }
        return
      }

      if (liveGuideView === 'categories' || liveGuideView === 'groups') {
        const inGroups = liveGuideView === 'groups' || focusCol === 'groups'
        if (dir === 'left') {
          if (!inGroups) {
            setLiveGuideView('groups')
            setFocusCol('groups')
          } else openMenu({ resumeGuide: true })
          return
        }
        if (dir === 'right') {
          if (inGroups) {
            setLiveGuideView('categories')
            setFocusCol('channels')
          } else {
            setLiveGuideView('channels')
            setFocusCol('channels')
          }
          return
        }
        if (dir === 'up' || dir === 'down') {
          const step = dir === 'down' ? 1 : -1
          if (inGroups) {
            setGroupCursor((current) => {
              const next = wrapIndex(current + step, groups.length)
              const group = groups[next]
              if (group) {
                setGroupId(group.id)
                selectGroup(group.id)
                setChannelCursor(0)
              }
              return next
            })
          } else moveChannel(step)
        }
        return
      }
    }

    window.addEventListener('keydown', onKey, true)
    window.addEventListener('keyup', onKeyUp, true)
    return () => {
      window.removeEventListener('keydown', onKey, true)
      window.removeEventListener('keyup', onKeyUp, true)
      window.clearTimeout(okTimer.current)
    }
  }, [
    channelMenu,
    channels,
    commitFavoriteMove,
    visiblePrograms.length,
    days.length,
    daysOpen,
    focusCol,
    focusedChannel,
    focusedProgram,
    groupCursor,
    groupId,
    groups,
    list.length,
    liveGuideView,
    moveFavorite,
    movingFavoriteId,
    openChannelMenu,
    openMenu,
    playProgram,
    setLiveGuideOpen,
    selectChannel,
    selectGroup,
    setChannelMenu,
    setLiveGuideView,
    settings.keys?.liveGuide,
    settings.keys?.menu,
    startFavoriteMove,
    toggleFavorite,
    goBack,
    favorites,
  ])

  if (!liveGuideView) return null

  const start = Math.max(0, Math.floor(scrollTop / CHANNEL_ROW) - OVERSCAN)
  const end = Math.min(list.length, Math.ceil((scrollTop + viewport) / CHANNEL_ROW) + OVERSCAN)
  const slice = list.slice(start, end)

  const renderChannels = () => (
    <div
      ref={channelRef}
      className="scroll-thin min-h-0 flex-1 overflow-y-auto px-2"
      onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
    >
      <div style={{ height: Math.max(list.length, 1) * CHANNEL_ROW, position: 'relative' }}>
        {slice.map((channel, offset) => {
          const index = start + offset
          const watching = channel.id === selectedChannel?.id
          const hovered = index === channelCursor
          const program = getCurrentProgram(channel)
          const progress = getProgramProgress(program, now.getTime())
          const fav = favorites.includes(channel.id)

          return (
            <button
              key={channel.id}
              type="button"
              data-ch={index}
              style={{ top: index * CHANNEL_ROW, height: CHANNEL_ROW - 4 }}
              onClick={() => {
                setChannelCursor(index)
                selectChannel(channel.id)
              }}
              onMouseEnter={() => setChannelCursor(index)}
              onContextMenu={(event) => {
                event.preventDefault()
                setChannelCursor(index)
                openChannelMenu(channel, 0)
              }}
              className={`remote-hit absolute right-0 left-0 flex items-center gap-3 rounded-lg px-2 text-left ${
                movingFavoriteId === channel.id ? 'fav-moving' : hovered ? 'bg-accent/70' : watching ? 'bg-white/10' : 'hover:bg-white/8'
              }`}
            >
              <LogoMark name={channel.name} logo={channel.logo} size={40} />
              <div className="min-w-0 flex-1">
                <div className="flex items-center gap-2">
                  {watching ? <span className="text-[11px] text-sky-300">▶</span> : null}
                <div className="truncate text-[14px] font-medium">
                  {channel.number} {channel.displayName}
                </div>
                {fav ? <span className="text-[11px] text-amber-300">★</span> : null}
              </div>
              <MarqueeText text={program?.title || 'Прямой эфир'} active={watching || hovered} />
                {watching || hovered ? (
                  <div className="mt-1 h-1 overflow-hidden rounded-full bg-white/15">
                    <div className="h-full rounded-full bg-white/80" style={{ width: `${progress * 100}%` }} />
                  </div>
                ) : null}
              </div>
              <ArchiveMark visible={settings.archiveEnabled && channelAllowsArchive(channel)} />
            </button>
          )
        })}
      </div>
      {!list.length ? <div className="px-3 py-6 text-sm text-white/40">В этой категории нет каналов</div> : null}
    </div>
  )

  return (
    <div className="fixed inset-0 z-[45]" onClick={() => setLiveGuideOpen(false)}>
      <div className="relative flex h-full items-stretch" onClick={(event) => event.stopPropagation()}>
        {liveGuideView === 'categories' || liveGuideView === 'groups' ? (
          <aside className="flex w-[min(220px,22vw)] flex-col bg-black/40 backdrop-blur-[2px]">
            <div className="px-4 py-3 text-[15px] font-medium text-sky-300">{activeGroup?.name}</div>
            <div className="scroll-thin flex-1 overflow-y-auto px-2 pb-3">
              {groups.map((group, index) => {
                const active = group.id === groupId
                const hovered = (liveGuideView === 'groups' || focusCol === 'groups') && index === groupCursor
                return (
                  <button
                    key={group.id}
                    type="button"
                    onClick={() => {
                      setGroupId(group.id)
                      setGroupCursor(index)
                      selectGroup(group.id)
                      setChannelCursor(0)
                      setLiveGuideView('categories')
                      setFocusCol('channels')
                    }}
                    className={`remote-hit mb-0.5 flex w-full items-center justify-between rounded-md px-3 py-2 text-left text-[14px] ${
                      hovered ? 'bg-accent/70 text-white' : active ? 'text-sky-300' : 'text-white/80 hover:bg-white/8'
                    }`}
                  >
                    <span className="truncate">{group.name}</span>
                    <span className="text-[11px] text-white/35">{group.count}</span>
                  </button>
                )
              })}
            </div>
          </aside>
        ) : null}

        {liveGuideView === 'categories' || liveGuideView === 'groups' || liveGuideView === 'channels' || liveGuideView === 'schedule' ? (
          <aside className="flex w-[min(280px,26vw)] flex-col bg-black/40 backdrop-blur-[2px]">
            <div className="px-4 py-3 text-[16px] font-medium">{activeGroup?.name || 'Каналы'}</div>
            {renderChannels()}
          </aside>
        ) : null}

        {liveGuideView === 'channels' || liveGuideView === 'schedule' ? (
          <>
            <aside className="flex w-[min(260px,24vw)] flex-col bg-black/35 backdrop-blur-[2px]">
              <div className="flex items-center gap-3 px-3 py-3">
                <LogoMark name={focusedChannel?.name} logo={focusedChannel?.logo} size={32} />
                <div className="min-w-0 truncate text-[14px] font-medium">
                  {focusedChannel?.number} {focusedChannel?.displayName}
                </div>
              </div>
              <div ref={programRef} className="scroll-thin flex-1 overflow-y-auto px-2 pb-4" style={{ paddingBottom: '42vh' }}>
                {visiblePrograms.map((program, index) => {
                  const day = startOfDay(program.start)
                  const prevDay = index > 0 ? startOfDay(visiblePrograms[index - 1].start) : null
                  const hovered = focusCol === 'programs' && index === programCursor
                  const selected = index === programCursor
                  const current = program.start <= now.getTime() && now.getTime() < program.end
                  const past = program.end <= now.getTime()
                  const archive =
                    past &&
                    settings.archiveEnabled &&
                    channelAllowsArchive(focusedChannel) &&
                    programHasArchive(focusedChannel, program, now.getTime(), settings.archiveDays)
                  return (
                    <div key={program.id || program.start}>
                      {day !== prevDay ? (
                        <div className="px-2 py-1.5 text-[13px] font-medium text-sky-300">{formatDayLong(day)}</div>
                      ) : null}
                      <button
                        type="button"
                        data-prog={index}
                        onClick={() => {
                          setProgramCursor(index)
                          setFocusCol('programs')
                          const ok = playProgram(focusedChannel, program)
                          if (ok) setLiveGuideOpen(false)
                        }}
                        onMouseEnter={() => {
                          setProgramCursor(index)
                          setFocusCol('programs')
                        }}
                        className={`mb-0.5 flex w-full items-center gap-2 rounded-md px-2 py-1.5 text-left text-[13px] ${
                          hovered ? 'bg-accent/75 text-white' : selected ? 'bg-white/12 text-white' : current ? 'text-sky-300' : 'text-white/75 hover:bg-white/8'
                        }`}
                      >
                        <span className={`w-11 shrink-0 tabular-nums ${current && !hovered ? 'text-sky-300' : 'text-white/55'}`}>
                          {new Date(program.start).toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}
                        </span>
                        <MarqueeText text={program.title} active={hovered || current} />
                        <ArchiveMark visible={archive} />
                      </button>
                    </div>
                  )
                })}
                {!visiblePrograms.length ? <div className="px-2 py-4 text-sm text-white/40">Нет программы</div> : null}
              </div>
            </aside>
            {daysOpen ? (
            <aside className="flex w-[84px] shrink-0 flex-col bg-black/30 backdrop-blur-[2px]">
              <div className="px-1 py-3 text-center text-[11px] uppercase tracking-wider text-white/35">Дни</div>
              <div ref={dayRef} className="scroll-thin flex-1 overflow-y-auto px-1 pb-3">
                {days.map((day, index) => (
                  <div key={day} data-day={index}>
                    <DayButton
                      day={day}
                      hovered={focusCol === 'days' && index === dayCursor}
                      selected={index === dayCursor}
                      onClick={() => {
                        setDaysOpen(true)
                        setDayCursor(index)
                        setFocusCol('days')
                      }}
                      onEnter={() => {
                        setDayCursor(index)
                        setFocusCol('days')
                      }}
                    />
                  </div>
                ))}
              </div>
            </aside>
            ) : null}
          </>
        ) : null}

        {liveGuideView === 'channels' || liveGuideView === 'schedule' ? (
          <div className={`absolute top-5 right-5 z-20 ${focusCol === 'detail' ? '' : 'pointer-events-none'}`}>
            <DetailCard program={focusedProgram} now={now} focused={focusCol === 'detail'} cardRef={detailRef} />
          </div>
        ) : null}
        {channelMenu ? (
          <div className="absolute top-24 left-[min(420px,40vw)] z-30 min-w-[240px] rounded-xl border border-white/15 bg-[#10151e] p-2 shadow-2xl">
            <div className="px-3 py-1 text-[12px] text-white/40">
              {(channels.find((item) => item.id === channelMenu.channelId) || focusedChannel)?.displayName}
            </div>
            {menuItems(channels.find((item) => item.id === channelMenu.channelId) || focusedChannel).map((item, index) => (
              <button
                key={item.id}
                type="button"
                className={`remote-hit mb-0.5 w-full rounded-lg px-3 py-2 text-left text-[14px] ${
                  index === channelMenu.cursor ? 'bg-accent/80' : 'hover:bg-white/8'
                }`}
                onClick={() => {
                  const channel = channels.find((entry) => entry.id === channelMenu.channelId) || focusedChannel
                  setChannelMenu(null)
                  if (item.id === 'fav') toggleFavorite(channel.id)
                  if (item.id === 'move') startFavoriteMove(channel.id)
                }}
              >
                {item.title}
              </button>
            ))}
          </div>
        ) : null}
        {error ? (
          <div className="pointer-events-none absolute bottom-8 left-1/2 z-20 -translate-x-1/2 rounded-xl bg-black/75 px-4 py-2 text-sm text-red-200">
            {error}
          </div>
        ) : null}
      </div>
    </div>
  )
}
