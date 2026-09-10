export const SETTINGS_KEY = 'mirefir.settings'
export const BACKUP_VERSION = 1

export const ARCHIVE_DAYS = [1, 3, 5, 7, 14]

export const DEFAULT_SETTINGS = {
  playlists: [],
  activePlaylistId: '',
  epgUrl: '',
  epgSources: [],
  epgOffsetHours: 0,
  epgDays: 7,
  epgAutoUpdate: true,
  archiveEnabled: true,
  archiveDays: 7,
  autoplay: true,
  rememberVolume: true,
  confirmExit: true,
  voiceEnabled: true,
  showChannelNumbers: true,
  showEpgProgress: true,
  showProgramDesc: true,
  twoLineTitles: true,
  clockEnabled: true,
  clockPosition: 'top-right',
  clockSize: 'md',
  clockOpacity: 90,
  theme: 'dark',
  highlight: 'blue',
  panelOpacity: 90,
  fontSize: 'md',
  logoSize: 'md',
  guideHours: 6,
  guideRows: 6,
  channelSort: 'playlist',
  hiddenGroups: [],
  playlistUpdateHours: 6,
  epgUpdateHours: 6,
  userAgent: 'MirEfir/1.0',
  decoder: 'auto',
  bufferSec: 15,
  aspect: 'auto',
  externalPlayer: false,
  defaultAudio: 'ru',
  recordingsEnabled: true,
  recordingPath: '',
  parentalEnabled: false,
  parentalPin: '0000',
  lockSettings: false,
  lockedGroups: [],
  language: 'ru',
  keys: {
    menu: 'F2',
    mute: 'm',
    favorite: ' ',
    fullscreen: 'Enter',
    guide: 'g',
    search: 's',
    record: 'r',
    pip: 'p',
    voice: 'v',
    liveGuide: 'ArrowLeft',
  },
}

function migrateLegacyStorage() {
  if (localStorage.getItem('mirefir.migrated')) return
  for (const name of ['settings', 'favorites', 'volume', 'muted', 'playlistUrl', 'epgUrl', 'recordings', 'skipVersion']) {
    const next = `mirefir.${name}`
    const prev = localStorage.getItem(`oneplayer.${name}`)
    if (prev != null && localStorage.getItem(next) == null) localStorage.setItem(next, prev)
  }
  localStorage.setItem('mirefir.migrated', '1')
}

export function normalizeEpgSources(settings) {
  const list = Array.isArray(settings?.epgSources) ? settings.epgSources : []
  const cleaned = list
    .map((item, index) => ({
      id: String(item?.id || `epg-${index + 1}`),
      url: String(item?.url || '').trim(),
      enabled: Boolean(item?.enabled),
    }))
    .filter((item) => item.url)
  if (cleaned.length) return cleaned
  const fallback = String(settings?.epgUrl || '').trim()
  return fallback ? [{ id: 'primary', url: fallback, enabled: true }] : []
}

export function enabledEpgUrls(settings) {
  return normalizeEpgSources(settings)
    .filter((item) => item.enabled)
    .slice(0, 2)
    .map((item) => item.url)
}

export function xmltvWindow(settings) {
  return {
    backDays: Math.max(Number(settings?.epgDays) || 7, Number(settings?.archiveDays) || 7, 7),
    aheadDays: Math.max(Number(settings?.epgDays) || 7, 7),
  }
}

export function loadSettings() {
  try {
    migrateLegacyStorage()
    const raw = JSON.parse(localStorage.getItem(SETTINGS_KEY) || 'null')
    if (!raw || typeof raw !== 'object') return { ...DEFAULT_SETTINGS }
    const merged = {
      ...DEFAULT_SETTINGS,
      ...raw,
      keys: { ...DEFAULT_SETTINGS.keys, ...(raw.keys || {}) },
      playlists: Array.isArray(raw.playlists) ? raw.playlists : [],
      hiddenGroups: Array.isArray(raw.hiddenGroups) ? raw.hiddenGroups : [],
      lockedGroups: Array.isArray(raw.lockedGroups) ? raw.lockedGroups : [],
      recordingPath: !raw.recordingPath || raw.recordingPath === 'Recordings' ? '' : raw.recordingPath,
      epgUrl: raw.epgUrl || '',
    }
    merged.epgSources = normalizeEpgSources(merged)
    if (!merged.epgUrl) merged.epgUrl = enabledEpgUrls(merged)[0] || ''
    return merged
  } catch {
    return { ...DEFAULT_SETTINGS }
  }
}

export function saveSettings(settings) {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings))
  queuePersistFile()
}

let persistTimer = 0

export function queuePersistFile() {
  if (typeof window === 'undefined' || !window.mirefir?.savePersist) return
  window.clearTimeout(persistTimer)
  persistTimer = window.setTimeout(() => {
    window.mirefir.savePersist(buildBackup()).catch(() => {})
  }, 250)
}

export async function restorePersistFile() {
  if (typeof window === 'undefined' || !window.mirefir?.loadPersist) return false
  if (
    localStorage.getItem(SETTINGS_KEY) ||
    localStorage.getItem('mirefir.playlistUrl') ||
    localStorage.getItem('mirefir.playlistText')
  ) {
    return false
  }
  try {
    const data = await window.mirefir.loadPersist()
    if (!data) return false
    applyBackup(data)
    return true
  } catch {
    return false
  }
}

export function buildBackup(extra = {}) {
  return {
    version: BACKUP_VERSION,
    createdAt: new Date().toISOString(),
    settings: loadSettings(),
    favorites: JSON.parse(localStorage.getItem('mirefir.favorites') || '[]'),
    volume: localStorage.getItem('mirefir.volume'),
    muted: localStorage.getItem('mirefir.muted'),
    playlistUrl: localStorage.getItem('mirefir.playlistUrl'),
    playlistText: localStorage.getItem('mirefir.playlistText'),
    epgUrl: localStorage.getItem('mirefir.epgUrl'),
    history: (() => {
      try {
        return JSON.parse(localStorage.getItem('mirefir.history') || '[]')
      } catch {
        return []
      }
    })(),
    session: (() => {
      try {
        return JSON.parse(localStorage.getItem('mirefir.session') || 'null')
      } catch {
        return null
      }
    })(),
    ...extra,
  }
}

export function applyBackup(data) {
  if (!data || typeof data !== 'object') throw new Error('Файл резервной копии повреждён')
  if (data.settings) {
    localStorage.setItem(
      SETTINGS_KEY,
      JSON.stringify({
        ...DEFAULT_SETTINGS,
        ...data.settings,
        keys: { ...DEFAULT_SETTINGS.keys, ...(data.settings.keys || {}) },
      }),
    )
  }
  if (data.favorites) localStorage.setItem('mirefir.favorites', JSON.stringify(data.favorites))
  if (data.volume != null) localStorage.setItem('mirefir.volume', String(data.volume))
  if (data.muted != null) localStorage.setItem('mirefir.muted', String(data.muted))
  if (data.playlistUrl) localStorage.setItem('mirefir.playlistUrl', data.playlistUrl)
  if (data.playlistText) localStorage.setItem('mirefir.playlistText', data.playlistText)
  if (data.epgUrl) localStorage.setItem('mirefir.epgUrl', data.epgUrl)
  if (data.history) localStorage.setItem('mirefir.history', JSON.stringify(data.history))
  if (data.session) localStorage.setItem('mirefir.session', JSON.stringify(data.session))
  queuePersistFile()
}

export const CLOCK_POSITIONS = [
  { id: 'top-left', name: 'Слева сверху' },
  { id: 'top-right', name: 'Справа сверху' },
  { id: 'bottom-left', name: 'Слева снизу' },
  { id: 'bottom-right', name: 'Справа снизу' },
]

export const CLOCK_SIZES = [
  { id: 'sm', name: 'Маленькие' },
  { id: 'md', name: 'Средние' },
  { id: 'lg', name: 'Крупные' },
]

export const KEY_LABELS = {
  menu: 'Меню',
  mute: 'Звук вкл/выкл',
  favorite: 'Избранное',
  fullscreen: 'Полный экран',
  guide: 'Телепрограмма',
  search: 'Поиск',
  record: 'Запись',
  pip: 'Картинка в картинке',
  voice: 'Голосовой поиск',
  liveGuide: 'Телегид поверх эфира (←)',
}
