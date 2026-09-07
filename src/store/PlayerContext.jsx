import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react'
import { getCurrentProgram, getNextProgram } from '../lib/epg.js'
import { collectGroups, loadPlaylistFromFile, loadPlaylistFromUrl, parseM3U } from '../lib/m3uParser.js'
import { bindEpgToChannels, loadXmltv, mergeXmltv } from '../lib/xmltv.js'
import { decodeCloudCode, encodeCloudCode } from '../lib/cloudCode.js'
import { buildCatchupUrl, canPlayArchive } from '../lib/catchup.js'
import {
  applyBackup,
  buildBackup,
  enabledEpgUrls,
  loadSettings,
  normalizeEpgSources,
  queuePersistFile,
  restorePersistFile,
  saveSettings,
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
  const [isFullscreen, setIsFullscreen] = useState(false)
  const [isModalOpen, setIsModalOpen] = useState(false)
  const [watchHistory, setWatchHistory] = useState(readHistory)
  const recentIds = useMemo(() => watchHistory.map((item) => item.id), [watchHistory])
  const [favorites, setFavorites] = useState(readFavorites)
  const [status, setStatus] = useState('Добавьте плейлист')
  const [error, setError] = useState('')
  const [volume, setVolume] = useState(readVolume)
  const [muted, setMuted] = useState(false)
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
  const [channelMenu, setChannelMenu] = useState(null)
  const [movingFavoriteId, setMovingFavoriteId] = useState(null)
  const favoriteMoveSnapshot = useRef(null)

  const playlistGroups = useMemo(() => {
    const hidden = settings.hiddenGroups || []
    return collectGroups(channels).filter((group) => !hidden.includes(group.id))
  }, [channels, settings.hiddenGroups])

  const groups = useMemo(
    () => [
      { id: 'all', name: 'Все каналы', count: channels.length, system: true },
      { id: 'favorites', name: 'Избранное', count: favorites.length, system: true },
      { id: 'recent', name: 'Недавние', count: recentIds.length, system: true },
      ...playlistGroups,
    ],
    [channels.length, favorites.length, playlistGroups, recentIds.length],
  )

  const visibleChannels = useMemo(() => {
    const hidden = settings.hiddenGroups || []
    let list = channels.filter((channel) => !hidden.includes(channel.group))

    if (listMode === 'archive') {
      list = settings.archiveEnabled ? list.filter((channel) => (channel.catchupDays || 0) > 0) : []
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
    if (!opts.silent) {
      setFocusZone('channels')
      setIsFullscreen(false)
      setError('')
    }

    if (xmltvRef.current) {
      const bound = bindEpgToChannels(parsed.channels, xmltvRef.current)
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
      const programs = epg[channel.id] || epg[channel.epgId] || epg[channel.tvgId] || []
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
    const urls = (Array.isArray(url) ? url : [url]).map((item) => String(item || '').trim()).filter(Boolean)
    if (!urls.length) throw new Error('Укажите ссылку на XMLTV')
    setStatus('Загрузка EPG… это может занять минуту')
    const windowOpts = xmltvWindow(settingsRef.current)
    const parts = []
    const errors = []
    for (const target of urls.slice(0, 2)) {
      try {
        parts.push(await loadXmltv(target, windowOpts))
        upsertEpgSource(target, true)
      } catch (err) {
        errors.push(err.message || String(err))
      }
    }
    if (!parts.length) throw new Error(errors[0] || 'Не удалось загрузить телепрограмму')
    const xmltv = mergeXmltv(parts)
    xmltvRef.current = xmltv
    localStorage.setItem(EPG_URL_KEY, urls[0])
    setEpgUrl(urls[0])
    setSettingsState((current) => {
      const next = { ...current, epgUrl: urls[0], epgSources: normalizeEpgSources(current) }
      saveSettings(next)
      return next
    })
    queuePersistFile()

    setChannels((current) => {
      const bound = bindEpgToChannels(current, xmltv)
      setEpg(bound.epg)
      const categoryCount = new Set(bound.channels.map((channel) => channel.group)).size
      setStatus(`${bound.channels.length} каналов · ${categoryCount} категорий · EPG ${bound.matched}`)
      return bound.channels
    })
    if (errors.length) setError(`Часть источников не загрузилась: ${errors.join('; ')}`)
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
      if (!canPlayArchive(channel, program, now, settings.archiveEnabled ? settings.archiveDays : 0)) {
        setError('Архив для этого канала недоступен')
        return false
      }
      const url = buildCatchupUrl(channel, program.start, program.end)
      if (!url) {
        setError('Не удалось собрать ссылку архива')
        return false
      }
      const originStart = options.originStart || program.start
      const originEnd = options.originEnd || program.end
      setSelectedChannelId(channel.id)
      rememberChannel(channel)
      setStreamOverride({
        url,
        mode: 'archive',
        start: program.start,
        end: program.end,
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
      const url = buildCatchupUrl(channel, nextStart, nextEnd)
      if (!url) return current
      return {
        ...current,
        url,
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
    if (now - backLock.current < 250) return 'skip'
    backLock.current = now
    if (exitPrompt) {
      setExitPrompt(false)
      return 'exit-prompt'
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
    if (isFullscreen) {
      setIsFullscreen(false)
      return 'player'
    }
    setUiScreen('menu')
    return 'menu'
  }, [exitPrompt, isFullscreen, isModalOpen, liveGuideView, menuResumeGuide, movingFavoriteId, channelMenu, uiScreen])

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

  const exportBackup = useCallback(() => {
    const backup = buildBackup({ favorites, settings })
    const blob = new Blob([JSON.stringify(backup, null, 2)], { type: 'application/json' })
    const link = document.createElement('a')
    link.href = URL.createObjectURL(blob)
    link.download = `mirefir-backup-${new Date().toISOString().slice(0, 10)}.json`
    link.click()
    URL.revokeObjectURL(link.href)
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
    })
  }, [channels, epgUrl, favorites, playlistUrl, settings.epgUrl, settings.playlists])

  const importCloudCode = useCallback(
    async (code) => {
      const data = decodeCloudCode(code)
      const playlists = data.playlistUrls.map((url, index) => ({
        id: index === 0 ? 'default' : `cloud-${index}`,
        name: `Плейлист ${index + 1}`,
        url,
      }))
      const next = {
        ...loadSettings(),
        playlists: playlists.length ? playlists : loadSettings().playlists,
        activePlaylistId: playlists[0]?.id || 'default',
        epgUrl: data.epgUrl || loadSettings().epgUrl,
      }
      saveSettings(next)
      setSettingsState(next)
      if (data.epgUrl) setEpgUrl(data.epgUrl)
      const url = playlists[0]?.url
      const parsed = url ? await importFromUrl(url) : null
      if (data.epgUrl) {
        try {
          await importEpg(data.epgUrl)
        } catch {
          /* playlist already restored */
        }
      }
      setChannels((current) => {
        const pool = current.length ? current : parsed?.channels || []
        const ids = data.favoriteNames
          .map((name) => pool.find((channel) => (channel.displayName || channel.name) === name)?.id)
          .filter(Boolean)
        localStorage.setItem(FAVORITES_KEY, JSON.stringify(ids))
        setFavorites(ids)
        return current
      })
    },
    [importEpg, importFromUrl],
  )

  const importBackupFile = useCallback(async (file) => {
    const data = JSON.parse(await file.text())
    applyBackup(data)
    const next = loadSettings()
    setSettingsState(next)
    setFavorites(JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]'))
    if (next.epgUrl) setEpgUrl(next.epgUrl)
    if (data.playlistUrl || next.playlists[0]?.url) {
      await importFromUrl(data.playlistUrl || next.playlists.find((item) => item.id === next.activePlaylistId)?.url || next.playlists[0].url)
    }
  }, [importFromUrl])

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

  const bootstrapped = useRef(false)
  const [bootReady, setBootReady] = useState(false)

  useEffect(() => {
    if (bootstrapped.current) return
    bootstrapped.current = true
    let cancelled = false

    const start = async () => {
      await restorePersistFile()
      if (cancelled) return
      setFavorites(readFavorites())
      setWatchHistory(readHistory())

      const url = readPlaylistUrl()
      const savedText = readPlaylistText()
      setPlaylistUrl(url)
      setEpgUrl(readEpgUrl() || loadSettings().epgUrl)
      setSettingsState(loadSettings())
      setBootReady(true)
      if (!url && !savedText) {
        setStatus('Добавьте плейлист')
        setIsModalOpen(true)
        return
      }

      if (savedText) {
        try {
          await applyPlaylist(parseM3U(savedText, 'Плейлист'))
        } catch {
          /* cache unreadable — try the URL */
        }
      } else {
        setStatus('Загрузка плейлиста…')
      }

      try {
        if (url) {
          const parsed = await loadPlaylistFromUrl(url)
          if (parsed.rawText) {
            localStorage.setItem(PLAYLIST_TEXT_KEY, parsed.rawText)
            queuePersistFile()
          }
          persistPlaylistUrl(url)
          await applyPlaylist(parsed, { silent: Boolean(savedText) })
        } else if (!savedText) {
          throw new Error('no playlist')
        }
        const saved = loadSettings()
        const guides = enabledEpgUrls(saved)
        const guide = guides[0] || readEpgUrl() || saved.epgUrl
        if (!guide && !guides.length) return
        importEpg(guides.length ? guides : guide).catch((err) => {
          setError(err.message || 'Не удалось загрузить телепрограмму. Её можно добавить позже.')
        })
      } catch {
        if (savedText) {
          setStatus('Плейлист из памяти. Обновление по ссылке не удалось')
          return
        }
        setStatus('Плейлист сохранён, повторная загрузка не удалась')
        setError('Не удалось открыть плейлист. Ссылка или файл уже сохранены — повторите позже, вводить заново не нужно.')
      }
    }

    start()
    return () => {
      cancelled = true
    }
  }, [applyPlaylist, importEpg])

  const value = {
    needsSetup: bootReady && channels.length === 0 && !hasSavedPlaylist(),
    playlistName,
    playlistUrl,
    epgUrl,
    channels,
    recentIds,
    watchHistory,
    clearHistory,
    groups,
    playlistGroups,
    visibleChannels,
    selectedGroupId,
    selectedChannel,
    streamUrl: streamOverride?.url || selectedChannel?.url || '',
    playback: streamOverride,
    playProgram,
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
    voiceArmed,
    setVoiceArmed,
    requestVoiceSearch,
    importFromText,
    importFromUrl,
    importFromFile,
    importEpg,
    upsertEpgSource,
    getPrograms: (channel) => shiftPrograms(epg[channel?.id] || epg[channel?.epgId] || epg[channel?.tvgId] || []),
    getCurrentProgram: (channel) =>
      getCurrentProgram(shiftPrograms(epg[channel?.id] || epg[channel?.epgId] || epg[channel?.tvgId] || [])),
    getNextProgram: (channel) =>
      getNextProgram(shiftPrograms(epg[channel?.id] || epg[channel?.epgId] || epg[channel?.tvgId] || [])),
  }

  return <PlayerContext.Provider value={value}>{children}</PlayerContext.Provider>
}

export function usePlayer() {
  const ctx = useContext(PlayerContext)
  if (!ctx) throw new Error('usePlayer must be used inside PlayerProvider')
  return ctx
}
