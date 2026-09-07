import { useEffect, useMemo, useRef, useState } from 'react'
import {
  collectGuideDays,
  formatDayShort,
  formatRange,
  formatRemaining,
  getProgramProgress,
  programsOnDay,
  startOfDay,
} from '../lib/epg.js'
import { arrowDir, isBackKey, isConfirmKey, isMenuKey } from '../lib/remoteKeys.js'
import { useClock } from '../hooks/useClock.js'
import { usePlayer } from '../store/PlayerContext.jsx'
import { LogoMark } from './LogoMark.jsx'

const CHANNEL_ROW = 72
const PROGRAM_ROW = 42
const OVERSCAN = 6

function wrapIndex(index, length) {
  if (!length) return 0
  return (index + length) % length
}

function ArchiveMark({ visible }) {
  if (!visible) return null
  return (
    <span className="ml-auto shrink-0 rounded-full border border-white/25 px-1.5 text-[10px] leading-4 text-white/55" title="Архив">
      ↺
    </span>
  )
}

function DetailCard({ program, now }) {
  if (!program) {
    return (
      <div className="w-[min(320px,28vw)] rounded-2xl bg-black/55 px-4 py-3 backdrop-blur-[2px]">
        <div className="text-[16px] text-white/70">Нет описания</div>
      </div>
    )
  }

  const progress = getProgramProgress(program, now.getTime())
  const remaining = formatRemaining(program, now.getTime())
  const live = program.start <= now.getTime() && now.getTime() < program.end

  return (
    <div className="w-[min(320px,28vw)] rounded-2xl bg-black/55 px-4 py-3 backdrop-blur-[2px]">
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
      {program.description ? <div className="mt-2 text-[13px] leading-relaxed text-white/75">{program.description}</div> : null}
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
  } = usePlayer()

  const [groupId, setGroupId] = useState(selectedGroupId || 'all')
  const [channelCursor, setChannelCursor] = useState(0)
  const [programCursor, setProgramCursor] = useState(0)
  const [groupCursor, setGroupCursor] = useState(0)
  const [dayCursor, setDayCursor] = useState(0)
  const [focusCol, setFocusCol] = useState('channels')
  const [scrollTop, setScrollTop] = useState(0)
  const [viewport, setViewport] = useState(640)
  const channelRef = useRef(null)
  const programRef = useRef(null)
  const openedRef = useRef(false)
  const okTimer = useRef(0)
  const okHeld = useRef(false)

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
  const days = useMemo(
    () =>
      collectGuideDays({
        programs: allPrograms,
        now: now.getTime(),
        archiveDays: settings.archiveDays,
        archiveEnabled: settings.archiveEnabled,
        catchupDays: focusedChannel?.catchupDays,
        epgDays: settings.epgDays,
      }),
    [allPrograms, focusedChannel?.catchupDays, now, settings.archiveDays, settings.archiveEnabled, settings.epgDays],
  )
  const selectedDay = days[dayCursor] || startOfDay(now.getTime())
  const dayPrograms = useMemo(() => programsOnDay(allPrograms, selectedDay), [allPrograms, selectedDay])
  const focusedProgram = dayPrograms[programCursor] || getCurrentProgram(focusedChannel)
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
    setFocusCol(liveGuideView === 'groups' ? 'groups' : 'channels')
    requestAnimationFrame(() => {
      const el = channelRef.current
      if (!el) return
      const top = Math.max(0, index * CHANNEL_ROW - el.clientHeight / 2 + CHANNEL_ROW)
      el.scrollTop = top
      setScrollTop(top)
    })
  }, [channels, favorites, groups, liveGuideView, recentIds, selectedChannel?.id, selectedGroupId, settings.hiddenGroups])

  useEffect(() => {
    if (!dayPrograms.length) {
      setProgramCursor(0)
      return
    }
    const current = getCurrentProgram(focusedChannel)
    const index = Math.max(
      0,
      dayPrograms.findIndex((item) => item.id === current?.id || (current && item.start === current.start)),
    )
    setProgramCursor(index)
  }, [focusedChannel?.id, selectedDay, liveGuideView])

  useEffect(() => {
    if (!liveGuideView) return
    const today = startOfDay(Date.now())
    const index = days.findIndex((day) => day === today)
    if (index >= 0) setDayCursor(index)
  }, [liveGuideView])

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
    const el = programRef.current
    if (!el || liveGuideView !== 'schedule') return
    const top = programCursor * PROGRAM_ROW
    if (top < el.scrollTop) el.scrollTop = top
    else if (top + PROGRAM_ROW > el.scrollTop + el.clientHeight) el.scrollTop = top + PROGRAM_ROW - el.clientHeight
  }, [liveGuideView, programCursor])

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
      if (liveGuideView === 'schedule' && focusCol === 'programs') {
        playProgram(focusedChannel, focusedProgram)
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

      if (liveGuideView === 'channels') {
        if (dir === 'left' || event.key === settings.keys?.liveGuide) {
          setLiveGuideView('categories')
          setFocusCol('channels')
          return
        }
        if (dir === 'right') {
          setLiveGuideView('schedule')
          setFocusCol('programs')
          return
        }
        if (dir === 'up' || dir === 'down') moveChannel(dir === 'down' ? 1 : -1)
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

      if (liveGuideView === 'schedule') {
        if (dir === 'left') {
          if (focusCol === 'days') setFocusCol('programs')
          else setLiveGuideView('channels')
          return
        }
        if (dir === 'right') {
          if (focusCol === 'programs' && days.length) setFocusCol('days')
          return
        }
        if (dir === 'up' || dir === 'down') {
          const step = dir === 'down' ? 1 : -1
          if (focusCol === 'days') setDayCursor((current) => wrapIndex(current + step, days.length))
          else setProgramCursor((current) => wrapIndex(current + step, dayPrograms.length))
        }
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
    dayPrograms.length,
    days.length,
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
                <div className="truncate text-[12px] text-white/70">{program?.title || 'Прямой эфир'}</div>
                {watching || hovered ? (
                  <div className="mt-1 h-1 overflow-hidden rounded-full bg-white/15">
                    <div className="h-full rounded-full bg-white/80" style={{ width: `${progress * 100}%` }} />
                  </div>
                ) : null}
              </div>
              <ArchiveMark visible={(channel.catchupDays || 0) > 0} />
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

        {liveGuideView === 'categories' || liveGuideView === 'groups' || liveGuideView === 'channels' ? (
          <aside className="flex w-[min(400px,36vw)] flex-col bg-black/40 backdrop-blur-[2px]">
            <div className="px-4 py-3 text-[16px] font-medium">{activeGroup?.name || 'Каналы'}</div>
            {renderChannels()}
          </aside>
        ) : null}

        {liveGuideView === 'channels' ? (
          <aside className="flex w-[min(340px,28vw)] flex-col bg-black/35 backdrop-blur-[2px]">
            <div className="flex items-center gap-3 px-4 py-3">
              <LogoMark name={focusedChannel?.name} logo={focusedChannel?.logo} size={36} />
              <div className="min-w-0">
                <div className="truncate text-[15px] font-medium">
                  {focusedChannel?.number} {focusedChannel?.displayName}
                </div>
              </div>
            </div>
            <div className="scroll-thin flex-1 overflow-y-auto px-3 pb-4">
              {dayPrograms.map((program) => {
                const current = program.start <= now.getTime() && now.getTime() < program.end
                return (
                  <div key={program.id || program.start} className={`flex gap-3 rounded-md px-2 py-1.5 text-[13px] ${current ? 'bg-white/12 text-white' : 'text-white/70'}`}>
                    <span className="w-12 shrink-0 tabular-nums text-white/50">
                      {new Date(program.start).toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}
                    </span>
                    <span className="truncate">{program.title}</span>
                    <ArchiveMark visible={(focusedChannel?.catchupDays || 0) > 0 && program.end <= now.getTime()} />
                  </div>
                )
              })}
              {!dayPrograms.length ? <div className="px-2 py-4 text-sm text-white/40">Нет программы</div> : null}
            </div>
          </aside>
        ) : null}

        {liveGuideView === 'schedule' ? (
          <>
            <aside className="flex w-[min(420px,38vw)] flex-col bg-black/40 backdrop-blur-[2px]">
              <div className="flex items-center gap-3 px-4 py-3">
                <LogoMark name={focusedChannel?.name} logo={focusedChannel?.logo} size={36} />
                <div className="min-w-0 text-[15px] font-medium">
                  {focusedChannel?.number} {focusedChannel?.displayName}
                </div>
              </div>
              <div ref={programRef} className="scroll-thin flex-1 overflow-y-auto px-2 pb-4">
                {dayPrograms.map((program, index) => {
                  const hovered = focusCol === 'programs' && index === programCursor
                  const current = program.start <= now.getTime() && now.getTime() < program.end
                  return (
                    <button
                      key={program.id || program.start}
                      type="button"
                      onClick={() => {
                        setProgramCursor(index)
                        playProgram(focusedChannel, program)
                      }}
                      onMouseEnter={() => {
                        setProgramCursor(index)
                        setFocusCol('programs')
                      }}
                      className={`remote-hit mb-0.5 flex w-full items-center gap-3 rounded-md px-3 py-2 text-left text-[14px] ${
                        hovered ? 'bg-accent/75' : current ? 'bg-white/10' : 'text-white/75 hover:bg-white/8'
                      }`}
                    >
                      <span className="w-12 shrink-0 tabular-nums text-white/55">
                        {new Date(program.start).toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })}
                      </span>
                      <span className="min-w-0 flex-1 truncate">{program.title}</span>
                      <ArchiveMark visible={(focusedChannel?.catchupDays || 0) > 0 && program.end <= now.getTime()} />
                    </button>
                  )
                })}
                {!dayPrograms.length ? <div className="px-3 py-6 text-sm text-white/40">Нет программы на этот день</div> : null}
              </div>
            </aside>
            <aside className="flex w-[150px] flex-col bg-black/35 backdrop-blur-[2px]">
              <div className="px-3 py-3 text-[12px] uppercase tracking-wider text-white/35">Дни</div>
              <div className="scroll-thin flex-1 overflow-y-auto px-2 pb-3">
                {days.map((day, index) => {
                  const hovered = focusCol === 'days' && index === dayCursor
                  return (
                    <button
                      key={day}
                      type="button"
                      onClick={() => {
                        setDayCursor(index)
                        setFocusCol('days')
                      }}
                      className={`remote-hit mb-0.5 w-full rounded-md px-2 py-2 text-left text-[13px] ${
                        hovered ? 'bg-accent/75' : index === dayCursor ? 'text-sky-300' : 'text-white/70 hover:bg-white/8'
                      }`}
                    >
                      {formatDayShort(day)}
                    </button>
                  )
                })}
              </div>
            </aside>
          </>
        ) : null}

        {liveGuideView === 'channels' || liveGuideView === 'schedule' ? (
          <div className="pointer-events-none absolute top-5 right-5 z-10">
            <DetailCard program={focusedProgram} now={now} />
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
      </div>
    </div>
  )
}
