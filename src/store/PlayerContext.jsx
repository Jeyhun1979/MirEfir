import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react'
import { getCurrentProgram, getNextProgram } from '../lib/epg.js'
import { collectGroups, loadPlaylistFromFile, loadPlaylistFromUrl, parseM3U } from '../lib/m3uParser.js'
import { bindEpgToChannels, loadXmltv, slimXmltv } from '../lib/xmltv.js'
import { decodeCloudCode, encodeCloudCode, readBackupPayload, shareOrSaveJson } from '../lib/cloudCode.js'
import { buildCatchupUrl, catchupUrlCandidates, canPlayArchive, channelHasCatchup } from '../lib/catchup.js'
import { quitApp } from '../lib/quitApp.js'
import { epgCacheIsFresh, loadEpgCache, readEpgCacheMeta, saveEpgCache, uniqueEpgUrls } from '../lib/epgCache.js'
import {
  applyBackup,
  buildBackup,
  DEFAULT_SETTINGS,
  enabledEpgUrls,
  loadSettings,
  normalizeEpgSources,
  queuePersistFile,
  restorePersistFile,
  saveSettings,
  writeEpgSlots,
  xmltvWindow,
} from '../lib/settingsStore.js'

const PlayerContext = createContext(null)
const FAVORITES_KEY = 'mirefir.favorites'
const SESSION_KEY = 'mirefir.session'
const VOLUME_KEY = 'mirefir.volume'
const MUTE_KEY = 'mirefir.muted'
const VOLUME_STEP = 0.05
const PLAYLIST_URL_KEY = 'mirefir.playlistUrl'
const PLAYLIST_TEXT_KEY = 'mirefir.playlistText'
const EPG_URL_KEY = 'mirefir.epgUrl'

function readPlaylistUrl() {
  return (
    localStorage.getItem(PLAYLIST_URL_KEY) ||
    loadSettings().playlists?.find((item) => item.url)?.url ||
    ''
  )
}

function readPlaylistText() {
  return localStorage.getItem(PLAYLIST_TEXT_KEY) || ''
}

function hasSavedPlaylist() {
  return Boolean(readPlaylistUrl() || readPlaylistText())
}

function readEpgUrl() {
  return localStorage.getItem(EPG_URL_KEY) || ''
}

function readVolume() {
  const raw = localStorage.getItem(VOLUME_KEY)
  const value = Number(raw)
  if (!localStorage.getItem('mirefir.volumeV2')) {
    localStorage.setItem('mirefir.volumeV2', '1')
    if (!raw || value === 0.8) {
      localStorage.setItem(VOLUME_KEY, '0.2')
      return 0.2
    }
  }
  if (!Number.isFinite(value) || value <= 0) return 0.2
  return Math.min(1, Math.max(0.05, value))
}

function readSession() {
  try {
    return JSON.parse(localStorage.getItem(SESSION_KEY) || 'null')
  } catch {
    return null
  }
}

function writeSession(data) {
  localStorage.setItem(SESSION_KEY, JSON.stringify(data))
}

function orderedFavorites(channels, favorites) {
  return favorites.map((id) => channels.find((channel) => channel.id === id)).filter(Boolean)
}

function findSessionChannel(channels, session) {
  if (!session || !channels?.length) return null
  return (
    channels.find((channel) => channel.id === session.channelId) ||
    channels.find((channel) => session.channelUrl && channel.url === session.channelUrl) ||
    channels.find(
      (channel) =>
        session.channelName &&
        (channel.name === session.channelName || channel.displayName === session.channelName),
    ) ||
    null
  )
}

function restoreFromSession(channels, groups, session) {
  const channel = findSessionChannel(channels, session)
  if (!channel) {
    return { channelId: channels[0]?.id || '', groupId: 'all', listMode: 'live' }
  }
  let groupId = session.groupId || 'all'
  const known =
    groupId === 'all' ||
    groupId === 'favorites' ||
    groupId === 'recent' ||
    (groups || []).some((group) => group.id === groupId) ||
    channels.some((item) => item.group === groupId)
  if (!known) groupId = channel.group || 'all'
  return { channelId: channel.id, groupId, listMode: session.listMode || 'live' }
}

function readFavorites() {
  try {
    return JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]')
  } catch {
    return []
  }
}

function matchFavoriteId(channels, name) {
  const target = String(name || '')
    .trim()
    .toLowerCase()
  if (!target) return ''
  const aliases = (channel) =>
    [channel.displayName, channel.name, channel.tvgName]
      .filter(Boolean)
      .map((value) => String(value).trim().toLowerCase())
  const exact = channels.find((channel) => aliases(channel).includes(target))
  if (exact) return exact.id
  const loose = channels.find((channel) => aliases(channel).some((item) => item.includes(target) || target.includes(item)))
  return loose?.id || ''
}

const HISTORY_KEY = 'mirefir.history'
const HISTORY_LIMIT = 10

function readHistory() {
  try {
    const raw = JSON.parse(localStorage.getItem(HISTORY_KEY) || '[]')
    if (!Array.isArray(raw)) return []
    return raw
      .map((item) => {
        if (typeof item === 'string') return { id: item, name: '', program: '', at: 0 }
        return {
          id: item.id,
          name: item.name || '',
          program: item.program || '',
          at: Number(item.at) || 0,
        }
      })
      .filter((item) => item.id)
      .slice(0, HISTORY_LIMIT)
  } catch {
    return []
  }
}

function writeHistory(list) {
  localStorage.setItem(HISTORY_KEY, JSON.stringify(list.slice(0, HISTORY_LIMIT)))
}

export function PlayerProvider({ children }) {
  const [playlistName, setPlaylistName] = useState('Плейлист')
  const [playlistUrl, setPlaylistUrl] = useState(readPlaylistUrl)
  const [epgUrl, setEpgUrl] = useState(readEpgUrl)
  const [channels, setChannels] = useState([])
  const [epg, setEpg] = useState({})
  const xmltvRef = useRef(null)
  const [selectedGroupId, setSelectedGroupId] = useState('all')
  const [selectedChannelId, setSelectedChannelId] = useState('')
  const [focusZone, setFocusZone] = useState('channels')
  const [hasPlaylist, setHasPlaylist] = useState(() => hasSavedPlaylist())
  const [isFullscreen, setIsFullscreen] = useState(() => hasSavedPlaylist())
  const [isModalOpen, setIsModalOpen] = useState(false)
  const [watchHistory, setWatchHistory] = useState(readHistory)
  const recentIds = useMemo(() => watchHistory.map((item) => item.id), [watchHistory])
  const [favorites, setFavorites] = useState(readFavorites)
  const [status, setStatus] = useState('Добавьте плейлист')
  const [error, setError] = useState('')
  const [volume, setVolume] = useState(readVolume)
  const [muted, setMuted] = useState(false)
  const [voiceDucked, setVoiceDucked] = useState(false)
  const [volumeTick, setVolumeTick] = useState(0)
  const [settings, setSettingsState] = useState(loadSettings)
  const settingsRef = useRef(settings)
  settingsRef.current = settings
  const [uiScreen, setUiScreen] = useState(null)
  const [settingsTab, setSettingsTab] = useState('playlists')
  const [listMode, setListMode] = useState('live')
  const [searchQuery, setSearchQuery] = useState('')
  const [recordingActive, setRecordingActive] = useState(false)
  const [recordPulse, setRecordPulse] = useState(0)
  const [liveGuideView, setLiveGuideView] = useState(null)
  const liveGuideOpen = Boolean(liveGuideView)
  const setLiveGuideOpen = useCallback((value) => {
    const open = typeof value === 'function' ? value(Boolean(liveGuideView)) : Boolean(value)
    setLiveGuideView(open ? liveGuideView || 'channels' : null)
  }, [liveGuideView])
  const [voiceArmed, setVoiceArmed] = useState(false)
  const [pipPulse, setPipPulse] = useState(0)
  const [exitPrompt, setExitPrompt] = useState(false)
  const [menuResumeGuide, setMenuResumeGuide] = useState(false)
  const [streamOverride, setStreamOverride] = useState(null)
  const catchupDeniedRef = useRef(new Set())
  const [catchupDeniedTick, setCatchupDeniedTick] = useState(0)
  const [channelMenu, setChannelMenu] = useState(null)
  const [movingFavoriteId, setMovingFavoriteId] = useState(null)
  const favoriteMoveSnapshot = useRef(null)
  const bootstrapped = useRef(false)
  const selectedIdRef = useRef('')
  const previousIdRef = useRef('')
  const epgBusy = useRef(false)
  const epgTimerRef = useRef(0)
  const importEpgRef = useRef(async () => {})
  const [bootScreen, setBootScreen] = useState(() => (typeof window !== 'undefined' && window.mirefir ? 'loading' : null))
  const [osFullscreen, setOsFullscreen] = useState(false)
  const [padOpen, setPadOpen] = useState(false)

  useEffect(() => {
    if (error !== 'Архив недоступен' && error !== 'Эта передача ещё не началась') return undefined
    const timer = window.setTimeout(() => setError(''), error === 'Архив недоступен' ? 2000 : 4500)
    return () => window.clearTimeout(timer)
  }, [error])

  const allPlaylistGroups = useMemo(() => collectGroups(channels), [channels])

  const playlistGroups = useMemo(() => {
    const hidden = settings.hiddenGroups || []
    return allPlaylistGroups.filter((group) => !hidden.includes(group.id))
  }, [allPlaylistGroups, settings.hiddenGroups])

  const groups = useMemo(() => {
    const hidden = settings.hiddenGroups || []
    const system = [
      { id: 'all', name: 'Все каналы', count: channels.length, system: true },
      { id: 'favorites', name: 'Избранное', count: favorites.length, system: true },
      { id: 'recent', name: 'Недавние', count: recentIds.length, system: true },
    ].filter((group) => !hidden.includes(group.id))
    return [...system, ...playlistGroups]
  }, [channels.length, favorites.length, playlistGroups, recentIds.length, settings.hiddenGroups])

  const visibleChannels = useMemo(() => {
    const hidden = settings.hiddenGroups || []
    let list = channels.filter((channel) => !hidden.includes(channel.group))

    if (listMode === 'archive') {
      list = settings.archiveEnabled ? list.filter((channel) => channelHasCatchup(channel)) : []
    } else if (listMode === 'movies') {
      list = list.filter((channel) => /кино|фильм|movie|cinema/i.test(channel.group))
    } else if (listMode === 'series') {
      list = list.filter((channel) => /сериал|series|show/i.test(channel.group))
    } else if (selectedGroupId === 'favorites' || listMode === 'favorites') {
      list = orderedFavorites(channels, favorites)
    } else if (selectedGroupId === 'recent' || listMode === 'history') {
      list = recentIds.map((id) => channels.find((channel) => channel.id === id)).filter(Boolean)
    } else if (selectedGroupId && selectedGroupId !== 'all') {
      list = channels.filter((channel) => channel.group === selectedGroupId)
    }

    if (searchQuery.trim()) {
      const q = searchQuery.trim().toLowerCase()
      list = channels.filter((channel) => (channel.displayName || channel.name || '').toLowerCase().includes(q))
    }

    const keepFavoriteOrder = selectedGroupId === 'favorites' || listMode === 'favorites'
    if (!keepFavoriteOrder) {
      if (settings.channelSort === 'name') {
        list = [...list].sort((a, b) => (a.displayName || a.name).localeCompare(b.displayName || b.name, 'ru'))
      } else if (settings.channelSort === 'number') {
        list = [...list].sort((a, b) => (a.number || 0) - (b.number || 0))
      }
    }

    return list
  }, [
    channels,
    favorites,
    listMode,
    recentIds,
    searchQuery,
    selectedGroupId,
    settings.archiveEnabled,
    settings.channelSort,
    settings.hiddenGroups,
  ])

  const selectedChannel = useMemo(
    () => channels.find((channel) => channel.id === selectedChannelId) || visibleChannels[0] || null,
    [channels, selectedChannelId, visibleChannels],
  )

  const applyPlaylist = useCallback(async (parsed, opts = {}) => {
    setPlaylistName(parsed.name)
    const restored = restoreFromSession(parsed.channels, parsed.groups || collectGroups(parsed.channels), readSession())
    setListMode(restored.listMode)
    setSelectedGroupId(restored.groupId)
    setSelectedChannelId(restored.channelId)
    if (parsed.channels?.length) setHasPlaylist(true)
    if (!opts.silent) {
      setFocusZone('channels')
      setError('')
      if (parsed.channels?.length) setIsFullscreen(true)
    }

    if (xmltvRef.current) {
      const bound = bindEpgToChannels(parsed.channels, xmltvRef.current)
      xmltvRef.current = slimXmltv(xmltvRef.current, bound.channels)
      setChannels(bound.channels)
      setEpg(bound.epg)
      const categoryCount = parsed.groups?.length || new Set(bound.channels.map((channel) => channel.group)).size
      setStatus(`${bound.channels.length} каналов · ${categoryCount} категорий · EPG ${bound.matched}`)
    } else {
      setChannels(parsed.channels)
      setEpg({})
      const categoryCount = parsed.groups?.length || new Set(parsed.channels.map((channel) => channel.group)).size
      setStatus(`${parsed.channels.length} каналов · ${categoryCount} категорий`)
    }
  }, [])

  const persistPlaylistUrl = useCallback((url) => {
    localStorage.setItem(PLAYLIST_URL_KEY, url)
    setPlaylistUrl(url)
    setSettingsState((current) => {
      const next = {
        ...current,
        playlists: [{ id: 'default', name: 'Основной', url }],
        activePlaylistId: 'default',
      }
      saveSettings(next)
      return next
    })
  }, [])

  const importFromText = useCallback(
    async (text, name) => {
      const parsed = parseM3U(text, name)
      localStorage.setItem(PLAYLIST_TEXT_KEY, text)
      localStorage.removeItem(PLAYLIST_URL_KEY)
      setPlaylistUrl('')
      queuePersistFile()
      await applyPlaylist(parsed)
    },
    [applyPlaylist],
  )

  const importFromUrl = useCallback(
    async (url) => {
      setStatus('Загрузка плейлиста…')
      const parsed = await loadPlaylistFromUrl(url)
      persistPlaylistUrl(url)
      if (parsed.rawText) {
        localStorage.setItem(PLAYLIST_TEXT_KEY, parsed.rawText)
        queuePersistFile()
      }
      await applyPlaylist(parsed)
      return parsed
    },
    [applyPlaylist, persistPlaylistUrl],
  )

  const importFromFile = useCallback(
    async (file) => {
      const text = await file.text()
      const parsed = parseM3U(text, file.name)
      localStorage.setItem(PLAYLIST_TEXT_KEY, text)
      localStorage.removeItem(PLAYLIST_URL_KEY)
      setPlaylistUrl('')
      queuePersistFile()
      await applyPlaylist(parsed)
    },
    [applyPlaylist],
  )

  const rememberChannel = useCallback(
    (channel) => {
      if (!channel?.id) return
      const programs = epg[channel.id] || []
      const offset = (settings.epgOffsetHours || 0) * 60 * 60 * 1000
      const shifted = offset
        ? programs.map((item) => ({ ...item, start: item.start + offset, end: item.end + offset }))
        : programs
      const program = getCurrentProgram(shifted)
      setWatchHistory((current) => {
        const next = [
          {
            id: channel.id,
            name: channel.displayName || channel.name || '',
            program: program?.title || 'Прямой эфир',
            at: Date.now(),
          },
          ...current.filter((item) => item.id !== channel.id),
        ].slice(0, HISTORY_LIMIT)
        writeHistory(next)
        queuePersistFile()
        return next
      })
    },
    [epg, settings.epgOffsetHours],
  )

  const clearHistory = useCallback(() => {
    writeHistory([])
    setWatchHistory([])
    queuePersistFile()
  }, [])

  const upsertEpgSource = useCallback((url, enabled = true) => {
    const nextUrl = String(url || '').trim()
    if (!nextUrl) return
    setSettingsState((current) => {
      let sources = normalizeEpgSources(current)
      const existing = sources.find((item) => item.url === nextUrl)
      if (existing) {
        if (enabled && !existing.enabled) {
          const count = sources.filter((item) => item.enabled).length
          if (count < 2) {
            sources = sources.map((item) => (item.id === existing.id ? { ...item, enabled: true } : item))
          }
        }
      } else {
        const count = sources.filter((item) => item.enabled).length
        sources = [...sources, { id: `epg-${Date.now()}`, url: nextUrl, enabled: enabled && count < 2 }]
      }
      const next = { ...current, epgSources: sources, epgUrl: enabledEpgUrls({ ...current, epgSources: sources })[0] || nextUrl }
      saveSettings(next)
      return next
    })
  }, [])

  const importEpg = useCallback(async (url) => {
    const urls = uniqueEpgUrls(url)
    if (!urls.length) throw new Error('Укажите ссылку на XMLTV')
    if (epgBusy.current) return
    epgBusy.current = true
    setStatus('Загрузка EPG… это может занять минуту')
    const windowOpts = xmltvWindow(settingsRef.current)
    try {
      let xmltv
      let used = urls[0]
      try {
        xmltv = await loadXmltv(urls[0], windowOpts)
      } catch (err) {
        if (!urls[1]) throw err
        setStatus('Основной источник не ответил, пробуем дополнительный…')
        xmltv = await loadXmltv(urls[1], windowOpts)
        used = urls[1]
      }
      upsertEpgSource(used, true)
      xmltvRef.current = xmltv
      localStorage.setItem(EPG_URL_KEY, used)
      setEpgUrl(used)
      setSettingsState((current) => {
        const next = { ...current, epgUrl: used, epgSources: normalizeEpgSources(current) }
        saveSettings(next)
        return next
      })
      queuePersistFile()

      setChannels((current) => {
        const bound = bindEpgToChannels(current, xmltv)
        xmltvRef.current = slimXmltv(xmltv, bound.channels)
        setEpg(bound.epg)
        const categoryCount = new Set(bound.channels.map((channel) => channel.group)).size
        setStatus(`${bound.channels.length} каналов · ${categoryCount} категорий · EPG ${bound.matched}`)
        return bound.channels
      })
      window.setTimeout(() => {
        saveEpgCache(xmltvRef.current, { url: used, ...windowOpts }).catch(() => {})
      }, 0)
    } finally {
      epgBusy.current = false
    }
  }, [upsertEpgSource])

  const selectGroup = useCallback(
    (groupId) => {
      setListMode('live')
      setSelectedGroupId(groupId)
      const nextChannel =
        groupId === 'all'
          ? channels[0]
          : groupId === 'favorites'
            ? orderedFavorites(channels, favorites)[0]
            : groupId === 'recent'
              ? channels.find((channel) => channel.id === recentIds[0])
              : channels.find((channel) => channel.group === groupId)
      if (nextChannel) setSelectedChannelId(nextChannel.id)
    },
    [channels, favorites, recentIds],
  )

  const selectChannel = useCallback(
    (channelId) => {
      setStreamOverride(null)
      setSelectedChannelId(channelId)
      setError('')
      rememberChannel(channels.find((item) => item.id === channelId))
    },
    [channels, rememberChannel],
  )

  const playProgram = useCallback(
    (channel, program, options = {}) => {
      if (!channel) return false
      const now = Date.now()
      if (!program || (program.start <= now && now < program.end && !options.timeshift)) {
        selectChannel(channel.id)
        return true
      }
      if (options.timeshift && now - program.start < 1500) {
        selectChannel(channel.id)
        return true
      }
      if (program.start > now && !options.timeshift) {
        setError('Эта передача ещё не началась')
        return false
      }
      if (!settings.archiveEnabled) {
        setError('Архив недоступен')
        return false
      }
      if (catchupDeniedRef.current.has(channel.id) || !canPlayArchive(channel, program, now, settings.archiveDays)) {
        setError('Архив недоступен')
        return false
      }
      const urls = catchupUrlCandidates(channel, program.start, program.end)
      const url = urls[0] || buildCatchupUrl(channel, program.start, program.end)
      if (!url) {
        setError('Архив недоступен')
        return false
      }
      const originStart = options.originStart || program.start
      const originEnd = options.originEnd || program.end
      const playEnd = Math.min(program.end, now - 1000)
      setSelectedChannelId(channel.id)
      rememberChannel(channel)
      setStreamOverride({
        url,
        urls,
        mode: 'archive',
        start: program.start,
        end: playEnd > program.start ? playEnd : program.end,
        originStart,
        originEnd,
        title: program.title,
        channelId: channel.id,
      })
      setError('')
      return true
    },
    [rememberChannel, selectChannel, settings.archiveDays, settings.archiveEnabled],
  )

  const failArchive = useCallback(() => {
    setStreamOverride(null)
    setError('Архив недоступен')
  }, [])

  const channelAllowsArchive = useCallback(
    (channel) => channelHasCatchup(channel) && !catchupDeniedRef.current.has(channel?.id),
    [catchupDeniedTick],
  )

  const seekToMs = useCallback(
    (targetMs, hint = {}) => {
      const channel = selectedChannel
      if (!channel || !Number.isFinite(targetMs)) return false
      const now = Date.now()
      const originStart = hint.originStart || streamOverride?.originStart || streamOverride?.start || hint.start
      const originEnd = hint.originEnd || streamOverride?.originEnd || streamOverride?.end || hint.end
      if (!originStart || !originEnd) return false
      const live = streamOverride?.mode !== 'archive'
      const maxMs = live ? now : originEnd
      const at = Math.min(maxMs, Math.max(originStart, targetMs))
      if (live && now - at < 1500) {
        selectChannel(channel.id)
        return true
      }
      return playProgram(
        channel,
        { start: at, end: originEnd, title: hint.title || streamOverride?.title || 'Архив' },
        { timeshift: true, originStart, originEnd },
      )
    },
    [playProgram, selectChannel, selectedChannel, streamOverride],
  )

  const seekArchive = useCallback((deltaSec) => {
    setStreamOverride((current) => {
      if (!current || current.mode !== 'archive') return current
      const span = current.end - current.start
      const nextStart = current.start + deltaSec * 1000
      const nextEnd = nextStart + span
      const channel = channels.find((item) => item.id === current.channelId)
      if (!channel) return current
      const urls = catchupUrlCandidates(channel, nextStart, nextEnd)
      const url = urls[0] || buildCatchupUrl(channel, nextStart, nextEnd)
      if (!url) return current
      return {
        ...current,
        url,
        urls,
        start: nextStart,
        end: nextEnd,
        originStart: current.originStart || current.start,
        originEnd: current.originEnd || current.end,
      }
    })
  }, [channels])

  const moveChannel = useCallback(
    (direction) => {
      if (!visibleChannels.length) return
      const index = Math.max(
        0,
        visibleChannels.findIndex((channel) => channel.id === selectedChannel?.id),
      )
      const next = visibleChannels[(index + direction + visibleChannels.length) % visibleChannels.length]
      selectChannel(next.id)
    },
    [selectChannel, selectedChannel, visibleChannels],
  )

  useEffect(() => {
    const id = selectedChannelId || ''
    if (!id || id === selectedIdRef.current) return
    if (selectedIdRef.current) previousIdRef.current = selectedIdRef.current
    selectedIdRef.current = id
  }, [selectedChannelId])

  const swapPreviousChannel = useCallback(() => {
    const prev = previousIdRef.current
    if (!prev || prev === selectedChannelId) return
    if (!channels.some((channel) => channel.id === prev)) return
    selectChannel(prev)
  }, [channels, selectChannel, selectedChannelId])

  const moveGroup = useCallback(
    (direction) => {
      if (!groups.length) return
      const index = Math.max(
        0,
        groups.findIndex((group) => group.id === selectedGroupId),
      )
      const next = groups[(index + direction + groups.length) % groups.length]
      selectGroup(next.id)
    },
    [groups, selectGroup, selectedGroupId],
  )

  const nudgeVolume = useCallback((direction) => {
    setVolume((current) => {
      const next = Math.min(1, Math.max(0, Math.round((current + direction * VOLUME_STEP) * 20) / 20))
      localStorage.setItem(VOLUME_KEY, String(next))
      setMuted(next === 0)
      localStorage.setItem(MUTE_KEY, next === 0 ? '1' : '0')
      return next
    })
    setVolumeTick((tick) => tick + 1)
  }, [])

  const toggleMute = useCallback(() => {
    setMuted((current) => {
      const next = !current
      localStorage.setItem(MUTE_KEY, next ? '1' : '0')
      return next
    })
    setVolumeTick((tick) => tick + 1)
  }, [])

  const updateSettings = useCallback((patch) => {
    setSettingsState((current) => {
      const next = typeof patch === 'function' ? patch(current) : { ...current, ...patch }
      saveSettings(next)
      return next
    })
  }, [])

  const openMenu = useCallback((opts) => {
    const resumeGuide = Boolean(opts && typeof opts === 'object' && opts.resumeGuide)
    setLiveGuideView(null)
    setExitPrompt(false)
    setMenuResumeGuide(resumeGuide)
    setUiScreen('menu')
  }, [])

  const openSettings = useCallback((tab = 'playlists') => {
    setLiveGuideOpen(false)
    setSettingsTab(tab)
    setUiScreen('settings')
  }, [])

  const closeOverlays = useCallback(() => {
    setUiScreen(null)
    setSearchQuery('')
    setVoiceArmed(false)
    setExitPrompt(false)
    setMenuResumeGuide(false)
    setChannelMenu(null)
    if (favoriteMoveSnapshot.current) {
      const snap = favoriteMoveSnapshot.current
      setFavorites(snap)
      localStorage.setItem(FAVORITES_KEY, JSON.stringify(snap))
      favoriteMoveSnapshot.current = null
    }
    setMovingFavoriteId(null)
    setLiveGuideOpen(false)
  }, [])

  const backLock = useRef(0)
  const goBack = useCallback(() => {
    const now = Date.now()
    if (!exitPrompt && now - backLock.current < 250) return 'skip'
    backLock.current = now
    if (error) {
      setError('')
      return 'error'
    }
    if (exitPrompt) {
      quitApp()
      return 'exit'
    }
    if (movingFavoriteId) {
      if (favoriteMoveSnapshot.current) {
        const snap = favoriteMoveSnapshot.current
        setFavorites(snap)
        localStorage.setItem(FAVORITES_KEY, JSON.stringify(snap))
        queuePersistFile()
        favoriteMoveSnapshot.current = null
      }
      setMovingFavoriteId(null)
      return 'fav-move'
    }
    if (channelMenu) {
      setChannelMenu(null)
      return 'channel-menu'
    }
    if (liveGuideView === 'groups') {
      setLiveGuideView('categories')
      return 'guide-groups'
    }
    if (liveGuideView === 'schedule' || liveGuideView === 'categories') {
      setLiveGuideView('channels')
      return 'guide-layer'
    }
    if (liveGuideView) {
      setLiveGuideView(null)
      return 'guide'
    }
    if (uiScreen === 'settings') {
      setUiScreen('menu')
      return 'settings'
    }
    if (uiScreen) {
      if (uiScreen === 'menu' && menuResumeGuide) {
        setUiScreen(null)
        setMenuResumeGuide(false)
        setLiveGuideView('groups')
        return 'guide-groups'
      }
      setUiScreen(null)
      setSearchQuery('')
      setVoiceArmed(false)
      setMenuResumeGuide(false)
      return 'overlay'
    }
    if (isModalOpen) {
      setIsModalOpen(false)
      return 'modal'
    }
    if (padOpen) {
      setPadOpen(false)
      return 'pad'
    }
    if (osFullscreen && window.mirefir?.setFullscreen) {
      window.mirefir.setFullscreen(false)
      return 'os-fs'
    }
    if (isFullscreen) {
      setExitPrompt(true)
      return 'exit-prompt'
    }
    setUiScreen('menu')
    return 'menu'
  }, [error, exitPrompt, isFullscreen, isModalOpen, liveGuideView, menuResumeGuide, movingFavoriteId, channelMenu, osFullscreen, padOpen, uiScreen])

  const requestRecord = useCallback(() => {
    setRecordPulse((value) => value + 1)
  }, [])

  const requestPip = useCallback(() => {
    setPipPulse((value) => value + 1)
  }, [])

  const requestVoiceSearch = useCallback(() => {
    setLiveGuideOpen(false)
    setVoiceArmed(true)
    setUiScreen('search')
  }, [])

  const toggleLiveGuide = useCallback(() => {
    if (!selectedChannelId) return
    setUiScreen(null)
    setIsFullscreen(true)
    setLiveGuideView((current) => (current ? null : 'channels'))
  }, [selectedChannelId])

  const exportBackup = useCallback(async () => {
    const backup = buildBackup({ favorites, settings })
    const fileName = `mirefir-backup-${new Date().toISOString().slice(0, 10)}.json`
    return shareOrSaveJson(fileName, backup)
  }, [favorites, settings])

  const exportCloudCode = useCallback(() => {
    const names = favorites
      .map((id) => channels.find((channel) => channel.id === id)?.displayName)
      .filter(Boolean)
    const urls = [
      ...(settings.playlists || []).map((item) => (typeof item === 'string' ? item : item.url)),
      playlistUrl,
    ].filter(Boolean)
    return encodeCloudCode({
      favoriteNames: names,
      playlists: [...new Set(urls)],
      epgUrl: settings.epgUrl || epgUrl,
      settings,
    })
  }, [channels, epgUrl, favorites, playlistUrl, settings])

  const importCloudCode = useCallback(
    async (code) => {
      const data = decodeCloudCode(code)
      const current = loadSettings()
      const incoming = data.settings && typeof data.settings === 'object' ? data.settings : {}
      const fromSettings = (incoming.playlists || [])
        .map((item) => (typeof item === 'string' ? item : item?.url))
        .filter(Boolean)
      const playlistUrls = data.playlistUrls.length ? data.playlistUrls : fromSettings
      const playlists = playlistUrls.map((url, index) => {
        const named = (incoming.playlists || []).find((item) => (typeof item === 'string' ? item : item?.url) === url)
        return {
          id: index === 0 ? 'default' : `cloud-${index}`,
          name: (named && named.name) || `Плейлист ${index + 1}`,
          url,
        }
      })
      let next = {
        ...current,
        ...incoming,
        playlists: playlists.length ? playlists : incoming.playlists || current.playlists,
        activePlaylistId: playlists[0]?.id || incoming.activePlaylistId || current.activePlaylistId || 'default',
        epgUrl: data.epgUrl || incoming.epgUrl || current.epgUrl,
        recordingPath: current.recordingPath,
        keys: { ...DEFAULT_SETTINGS.keys, ...(incoming.keys || current.keys) },
        hiddenGroups: Array.isArray(incoming.hiddenGroups) ? incoming.hiddenGroups : current.hiddenGroups,
      }
      if (data.epgUrl) next = writeEpgSlots(next, { primaryUrl: data.epgUrl })
      else next.epgSources = normalizeEpgSources(next)
      saveSettings(next)
      setSettingsState(next)
      if (next.epgUrl) setEpgUrl(next.epgUrl)
      const url = playlists[0]?.url
      const parsed = url ? await importFromUrl(url) : null
      saveSettings(next)
      setSettingsState(next)
      if (next.epgUrl) {
        try {
          await importEpg(next.epgUrl)
        } catch {
          /* playlist already restored */
        }
      }
      const pool = parsed?.channels?.length ? parsed.channels : channels
      const ids = data.favoriteNames.map((name) => matchFavoriteId(pool, name)).filter(Boolean)
      localStorage.setItem(FAVORITES_KEY, JSON.stringify(ids))
      setFavorites(ids)
      queuePersistFile()
    },
    [channels, importEpg, importFromUrl],
  )

  const importBackupFile = useCallback(
    async (file) => {
      const data = await readBackupPayload(file)
      applyBackup(data)
      const next = loadSettings()
      setSettingsState(next)
      setFavorites(JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]'))
      if (data.volume != null) {
        const value = Number(data.volume)
        if (Number.isFinite(value)) setVolume(Math.min(1, Math.max(0.05, value)))
      }
      if (data.muted != null) setMuted(data.muted === true || data.muted === 'true' || data.muted === '1')
      if (next.epgUrl) setEpgUrl(next.epgUrl)
      const url =
        data.playlistUrl ||
        next.playlists.find((item) => item.id === next.activePlaylistId)?.url ||
        next.playlists[0]?.url ||
        ''
      if (url) await importFromUrl(url)
      else if (data.playlistText) await importFromText(data.playlistText)
      saveSettings(next)
      setSettingsState(next)
      const epg = next.epgUrl || data.epgUrl
      if (epg) {
        try {
          await importEpg(epg)
        } catch {
          /* settings already applied */
        }
      }
    },
    [importEpg, importFromText, importFromUrl],
  )

  const shiftPrograms = useCallback(
    (programs) => {
      const offset = (settings.epgOffsetHours || 0) * 60 * 60 * 1000
      if (!offset || !programs?.length) return programs || []
      return programs.map((item) => ({ ...item, start: item.start + offset, end: item.end + offset }))
    },
    [settings.epgOffsetHours],
  )

  const toggleFavorite = useCallback((channelId) => {
    setFavorites((current) => {
      const next = current.includes(channelId)
        ? current.filter((id) => id !== channelId)
        : [...current, channelId]
      localStorage.setItem(FAVORITES_KEY, JSON.stringify(next))
      queuePersistFile()
      return next
    })
  }, [])

  const openChannelMenu = useCallback((channel, cursor = 0) => {
    if (!channel) return
    setChannelMenu({ channelId: channel.id, cursor })
  }, [])

  const startFavoriteMove = useCallback(
    (channelId) => {
      setFavorites((current) => {
        favoriteMoveSnapshot.current = [...current]
        return current
      })
      setSelectedChannelId(channelId)
      setMovingFavoriteId(channelId)
      setChannelMenu(null)
      setUiScreen(null)
      if (isFullscreen && !liveGuideView) setLiveGuideView('channels')
    },
    [isFullscreen, liveGuideView],
  )

  const commitFavoriteMove = useCallback(() => {
    favoriteMoveSnapshot.current = null
    setMovingFavoriteId(null)
    queuePersistFile()
  }, [])

  const moveFavorite = useCallback((channelId, direction) => {
    setFavorites((current) => {
      const index = current.indexOf(channelId)
      if (index < 0) return current
      const to = index + direction
      if (to < 0 || to >= current.length) return current
      const next = [...current]
      const [item] = next.splice(index, 1)
      next.splice(to, 0, item)
      localStorage.setItem(FAVORITES_KEY, JSON.stringify(next))
      queuePersistFile()
      return next
    })
  }, [])

  const channelMenuItems = useCallback(
    (channel, folderId = selectedGroupId) => {
      if (!channel) return []
      const starred = favorites.includes(channel.id)
      const items = [{ id: 'fav', title: starred ? 'Удалить из избранного' : 'Добавить в избранное' }]
      if ((folderId === 'favorites' || listMode === 'favorites') && starred) {
        items.push({ id: 'move', title: 'Переместить' })
      }
      return items
    },
    [favorites, listMode, selectedGroupId],
  )

  const runChannelMenuItem = useCallback(
    (channel, itemId) => {
      setChannelMenu(null)
      if (!channel || !itemId) return
      if (itemId === 'fav') toggleFavorite(channel.id)
      if (itemId === 'move') startFavoriteMove(channel.id)
    },
    [startFavoriteMove, toggleFavorite],
  )

  useEffect(() => {
    if (!selectedChannelId || !channels.length) return
    const channel = channels.find((item) => item.id === selectedChannelId)
    writeSession({
      channelId: selectedChannelId,
      channelUrl: channel?.url || '',
      channelName: channel?.name || channel?.displayName || '',
      groupId: selectedGroupId || 'all',
      listMode,
    })
    queuePersistFile()
  }, [channels, listMode, selectedChannelId, selectedGroupId])

  useEffect(() => {
    if (!groups.length) return
    if (groups.some((group) => group.id === selectedGroupId)) return
    setSelectedGroupId(groups[0].id)
  }, [groups, selectedGroupId])

  useEffect(() => {
    if (!window.mirefir?.onFullscreen) return undefined
    window.mirefir.isFullscreen?.().then((value) => setOsFullscreen(Boolean(value)))
    return window.mirefir.onFullscreen?.((value) => setOsFullscreen(Boolean(value)))
  }, [])

  useEffect(() => {
    importEpgRef.current = importEpg
  }, [importEpg])

  useEffect(() => {
    if (!settings.epgAutoUpdate) return undefined
    let cancelled = false
    const hours = Math.max(1, Number(settings.epgUpdateHours) || 6)
    const maxAge = hours * 60 * 60 * 1000

    const refresh = async () => {
      const urls = enabledEpgUrls(settingsRef.current)
      if (!urls.length || cancelled) return
      try {
        await importEpgRef.current(urls)
      } catch {
        /* keep last programme */
      }
    }

    const schedule = async () => {
      if (cancelled) return
      const meta = await readEpgCacheMeta()
      const wait = meta?.savedAt ? Math.max(20_000, maxAge - (Date.now() - meta.savedAt)) : maxAge
      window.clearTimeout(epgTimerRef.current)
      epgTimerRef.current = window.setTimeout(async () => {
        await refresh()
        if (!cancelled) schedule()
      }, wait)
    }

    schedule()
    return () => {
      cancelled = true
      window.clearTimeout(epgTimerRef.current)
    }
  }, [settings.epgAutoUpdate, settings.epgUpdateHours])

  useEffect(() => {
    if (bootstrapped.current) return
    bootstrapped.current = true
    let cancelled = false

    const finishBoot = () => {
      setBootScreen(null)
      window.mirefir?.clearInstallLock?.()
    }

    const attachCachedGuide = (xmltv, channelList) => {
      if (!xmltv || cancelled) return
      const bound = bindEpgToChannels(channelList, xmltv)
      xmltvRef.current = slimXmltv(xmltv, bound.channels)
      setChannels(bound.channels)
      setEpg(bound.epg)
    }

    const start = async () => {
      const flags = await window.mirefir?.launchFlags?.()
      if (cancelled) return
      if (flags?.fromUpdate) setBootScreen('install')

      await restorePersistFile()
      if (cancelled) return
      setFavorites(readFavorites())
      setWatchHistory(readHistory())
      if (hasSavedPlaylist()) {
        setHasPlaylist(true)
        setIsFullscreen(true)
      }

      const url = readPlaylistUrl()
      const savedText = readPlaylistText()
      setPlaylistUrl(url)
      setEpgUrl(readEpgUrl() || loadSettings().epgUrl)
      setSettingsState(loadSettings())
      if (!url && !savedText) {
        setStatus('Добавьте плейлист')
        setIsModalOpen(true)
        finishBoot()
        return
      }

      let parsedChannels = []
      let ready = false
      if (savedText) {
        try {
          const parsed = parseM3U(savedText, 'Плейлист')
          parsedChannels = parsed.channels || []
          await applyPlaylist(parsed)
          ready = parsedChannels.length > 0
        } catch {
          /* cache unreadable — try the URL */
        }
      }
      if (ready) finishBoot()
      else setStatus('Загрузка плейлиста…')

      const refreshInBackground = async () => {
        try {
          if (url) {
            const parsed = await loadPlaylistFromUrl(url)
            if (cancelled) return
            if (parsed.rawText) {
              localStorage.setItem(PLAYLIST_TEXT_KEY, parsed.rawText)
              queuePersistFile()
            }
            persistPlaylistUrl(url)
            await applyPlaylist(parsed, { silent: ready })
            parsedChannels = parsed.channels || parsedChannels
          } else if (!savedText) {
            throw new Error('no playlist')
          }
          if (!ready) finishBoot()

          window.setTimeout(async () => {
            if (cancelled) return
            const [cachedXmltv, meta] = await Promise.all([loadEpgCache(), readEpgCacheMeta()]).catch(() => [null, null])
            if (cancelled) return
            if (cachedXmltv) attachCachedGuide(cachedXmltv, parsedChannels)

            const latest = loadSettings()
            const guides = enabledEpgUrls(latest)
            const guide = guides[0] || readEpgUrl() || latest.epgUrl
            if (!guide && !guides.length) return
            if (cachedXmltv && epgCacheIsFresh(meta, latest)) return
            if (cancelled) return
            importEpg(guides.length ? guides : guide).catch((err) => {
              if (cachedXmltv) return
              setError(err.message || 'Не удалось загрузить телепрограмму. Её можно добавить позже.')
            })
          }, 400)
        } catch {
          if (savedText) {
            setStatus('Плейлист из памяти. Обновление по ссылке не удалось')
            window.setTimeout(async () => {
              const cachedXmltv = await loadEpgCache().catch(() => null)
              if (!cancelled && cachedXmltv) attachCachedGuide(cachedXmltv, parsedChannels)
            }, 400)
            return
          }
          setStatus('Плейлист сохранён, повторная загрузка не удалась')
          setError('Не удалось открыть плейлист. Ссылка или файл уже сохранены — повторите позже, вводить заново не нужно.')
          finishBoot()
        }
      }

      refreshInBackground()
    }

    start()
    return () => {
      cancelled = true
    }
  }, [applyPlaylist, importEpg])

  const value = {
    needsSetup: !hasPlaylist,
    playlistName,
    playlistUrl,
    epgUrl,
    channels,
    recentIds,
    watchHistory,
    clearHistory,
    groups,
    playlistGroups,
    allPlaylistGroups,
    visibleChannels,
    selectedGroupId,
    selectedChannel,
    streamUrl: bootScreen === 'install' ? '' : (streamOverride?.url || selectedChannel?.url || ''),
    playback: streamOverride,
    playProgram,
    failArchive,
    channelAllowsArchive,
    seekArchive,
    seekToMs,
    focusZone,
    isFullscreen,
    isModalOpen,
    uiScreen,
    settingsTab,
    listMode,
    searchQuery,
    settings,
    favorites,
    epg,
    status,
    error,
    setFocusZone,
    setIsFullscreen,
    setIsModalOpen,
    setUiScreen,
    setSettingsTab,
    setListMode,
    setSelectedGroupId,
    setSearchQuery,
    openMenu,
    openSettings,
    closeOverlays,
    goBack,
    exitPrompt,
    setExitPrompt,
    menuResumeGuide,
    updateSettings,
    exportBackup,
    exportCloudCode,
    importCloudCode,
    importBackupFile,
    setError,
    setStatus,
    selectGroup,
    selectChannel,
    swapPreviousChannel,
    moveChannel,
    moveGroup,
    toggleFavorite,
    channelMenu,
    setChannelMenu,
    openChannelMenu,
    channelMenuItems,
    runChannelMenuItem,
    movingFavoriteId,
    startFavoriteMove,
    commitFavoriteMove,
    moveFavorite,
    volume,
    muted,
    voiceDucked,
    setVoiceDucked,
    volumeTick,
    nudgeVolume,
    toggleMute,
    recordingActive,
    setRecordingActive,
    recordPulse,
    requestRecord,
    pipPulse,
    requestPip,
    liveGuideOpen,
    liveGuideView,
    setLiveGuideOpen,
    setLiveGuideView,
    toggleLiveGuide,
    bootScreen,
    osFullscreen,
    setOsFullscreen,
    padOpen,
    setPadOpen,
    voiceArmed,
    setVoiceArmed,
    requestVoiceSearch,
    importFromText,
    importFromUrl,
    importFromFile,
    importEpg,
    upsertEpgSource,
    getPrograms: (channel) => shiftPrograms(epg[channel?.id] || []),
    getCurrentProgram: (channel) =>
      getCurrentProgram(shiftPrograms(epg[channel?.id] || [])),
    getNextProgram: (channel) =>
      getNextProgram(shiftPrograms(epg[channel?.id] || [])),
  }

  return <PlayerContext.Provider value={value}>{children}</PlayerContext.Provider>
}

export function usePlayer() {
  const ctx = useContext(PlayerContext)
  if (!ctx) throw new Error('usePlayer must be used inside PlayerProvider')
  return ctx
}
