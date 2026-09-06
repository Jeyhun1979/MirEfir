import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react'
import { getCurrentProgram, getNextProgram } from '../lib/epg.js'
import { collectGroups, loadPlaylistFromFile, loadPlaylistFromUrl, parseM3U } from '../lib/m3uParser.js'
import { bindEpgToChannels, loadXmltv } from '../lib/xmltv.js'
import { decodeCloudCode, encodeCloudCode } from '../lib/cloudCode.js'
import { applyBackup, buildBackup, loadSettings, saveSettings } from '../lib/settingsStore.js'

const PlayerContext = createContext(null)
const FAVORITES_KEY = 'mirefir.favorites'
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

function readFavorites() {
  try {
    return JSON.parse(localStorage.getItem(FAVORITES_KEY) || '[]')
  } catch {
    return []
  }
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
  const [recentIds, setRecentIds] = useState([])
  const [favorites, setFavorites] = useState(readFavorites)
  const [status, setStatus] = useState('Добавьте плейлист')
  const [error, setError] = useState('')
  const [volume, setVolume] = useState(readVolume)
  const [muted, setMuted] = useState(false)
  const [volumeTick, setVolumeTick] = useState(0)
  const [settings, setSettingsState] = useState(loadSettings)
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
      list = channels.filter((channel) => favorites.includes(channel.id))
    } else if (selectedGroupId === 'recent' || listMode === 'history') {
      list = recentIds.map((id) => channels.find((channel) => channel.id === id)).filter(Boolean)
    } else if (selectedGroupId && selectedGroupId !== 'all') {
      list = channels.filter((channel) => channel.group === selectedGroupId)
    }

    if (searchQuery.trim()) {
      const q = searchQuery.trim().toLowerCase()
      list = channels.filter((channel) => (channel.displayName || channel.name || '').toLowerCase().includes(q))
    }

    if (settings.channelSort === 'name') {
      list = [...list].sort((a, b) => (a.displayName || a.name).localeCompare(b.displayName || b.name, 'ru'))
    } else if (settings.channelSort === 'number') {
      list = [...list].sort((a, b) => (a.number || 0) - (b.number || 0))
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

  const applyPlaylist = useCallback(async (parsed) => {
    setPlaylistName(parsed.name)
    setSelectedGroupId('all')
    const firstChannel = parsed.channels[0]
    setSelectedChannelId(firstChannel?.id || '')
    setRecentIds([])
    setFocusZone('channels')
    setIsFullscreen(false)
    setError('')

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
    localStorage.removeItem(PLAYLIST_TEXT_KEY)
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
      await applyPlaylist(parsed)
    },
    [applyPlaylist],
  )

  const importFromUrl = useCallback(
    async (url) => {
      setStatus('Загрузка плейлиста…')
      const parsed = await loadPlaylistFromUrl(url)
      persistPlaylistUrl(url)
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
      await applyPlaylist(parsed)
    },
    [applyPlaylist],
  )

  const importEpg = useCallback(async (url) => {
    const nextUrl = url.trim()
    if (!nextUrl) throw new Error('Укажите ссылку на XMLTV')
    setStatus('Загрузка EPG… это может занять минуту')
    const xmltv = await loadXmltv(nextUrl)
    xmltvRef.current = xmltv
    localStorage.setItem(EPG_URL_KEY, nextUrl)
    setEpgUrl(nextUrl)
    setSettingsState((current) => {
      const next = { ...current, epgUrl: nextUrl }
      saveSettings(next)
      return next
    })

    setChannels((current) => {
      const bound = bindEpgToChannels(current, xmltv)
      setEpg(bound.epg)
      const categoryCount = new Set(bound.channels.map((channel) => channel.group)).size
      setStatus(`${bound.channels.length} каналов · ${categoryCount} категорий · EPG ${bound.matched}`)
      return bound.channels
    })
  }, [])

  const selectGroup = useCallback(
    (groupId) => {
      setListMode('live')
      setSelectedGroupId(groupId)
      const nextChannel =
        groupId === 'all'
          ? channels[0]
          : groupId === 'favorites'
            ? channels.find((channel) => favorites.includes(channel.id))
            : groupId === 'recent'
              ? channels.find((channel) => channel.id === recentIds[0])
              : channels.find((channel) => channel.group === groupId)
      if (nextChannel) setSelectedChannelId(nextChannel.id)
    },
    [channels, favorites, recentIds],
  )

  const selectChannel = useCallback((channelId) => {
    setSelectedChannelId(channelId)
    setRecentIds((current) => [channelId, ...current.filter((id) => id !== channelId)].slice(0, 24))
  }, [])

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
  }, [exitPrompt, isFullscreen, isModalOpen, liveGuideView, menuResumeGuide, uiScreen])

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
      return next
    })
  }, [])

  const bootstrapped = useRef(false)

  useEffect(() => {
    if (bootstrapped.current) return
    bootstrapped.current = true

    const url = readPlaylistUrl()
    const savedText = readPlaylistText()
    if (!url && !savedText) {
      setStatus('Добавьте плейлист')
      setIsModalOpen(true)
      return
    }

    setStatus('Загрузка плейлиста…')
    const load = url ? loadPlaylistFromUrl(url) : Promise.resolve(parseM3U(savedText, 'Плейлист'))
    load
      .then(async (parsed) => {
        await applyPlaylist(parsed)
        const guide = readEpgUrl() || loadSettings().epgUrl
        if (!guide) return
        try {
          await importEpg(guide)
        } catch (err) {
          setError(err.message || 'Не удалось загрузить телепрограмму. Её можно добавить позже.')
        }
      })
      .catch(() => {
        setStatus('Добавьте плейлист')
        setIsModalOpen(true)
        setError('Не удалось открыть плейлист. Укажите ссылку или файл.')
      })
  }, [applyPlaylist, importEpg])

  const value = {
    needsSetup: channels.length === 0 && !hasSavedPlaylist(),
    playlistName,
    playlistUrl,
    epgUrl,
    channels,
    recentIds,
    groups,
    playlistGroups,
    visibleChannels,
    selectedGroupId,
    selectedChannel,
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
