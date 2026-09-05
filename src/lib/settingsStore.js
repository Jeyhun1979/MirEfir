export const SETTINGS_KEY = 'oneplayer.settings'
export const BACKUP_VERSION = 1

export const ARCHIVE_DAYS = [1, 3, 5, 7, 14]

export const DEFAULT_SETTINGS = {
  playlists: [],
  activePlaylistId: '',
  epgUrl: '',
  epgOffsetHours: 0,
  epgDays: 2,
  epgAutoUpdate: true,
  archiveEnabled: true,
  archiveDays: 7,
  autoplay: true,
  rememberVolume: true,
  confirmExit: true,
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
  userAgent: 'OnePlayer/1.0',
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

export function loadSettings() {
  try {
    const raw = JSON.parse(localStorage.getItem(SETTINGS_KEY) || 'null')
    if (!raw || typeof raw !== 'object') return { ...DEFAULT_SETTINGS }
    return {
      ...DEFAULT_SETTINGS,
      ...raw,
      keys: { ...DEFAULT_SETTINGS.keys, ...(raw.keys || {}) },
      playlists: Array.isArray(raw.playlists) ? raw.playlists : [],
      hiddenGroups: Array.isArray(raw.hiddenGroups) ? raw.hiddenGroups : [],
      lockedGroups: Array.isArray(raw.lockedGroups) ? raw.lockedGroups : [],
      recordingPath: !raw.recordingPath || raw.recordingPath === 'Recordings' ? '' : raw.recordingPath,
      epgUrl: raw.epgUrl || '',
    }
  } catch {
    return { ...DEFAULT_SETTINGS }
  }
}

export function saveSettings(settings) {
  localStorage.setItem(SETTINGS_KEY, JSON.stringify(settings))
}

export function buildBackup(extra = {}) {
  return {
    version: BACKUP_VERSION,
    createdAt: new Date().toISOString(),
    settings: loadSettings(),
    favorites: JSON.parse(localStorage.getItem('oneplayer.favorites') || '[]'),
    volume: localStorage.getItem('oneplayer.volume'),
    muted: localStorage.getItem('oneplayer.muted'),
    playlistUrl: localStorage.getItem('oneplayer.playlistUrl'),
    epgUrl: localStorage.getItem('oneplayer.epgUrl'),
    ...extra,
  }
}

export function applyBackup(data) {
  if (!data || typeof data !== 'object') throw new Error('Файл резервной копии повреждён')
  if (data.settings) saveSettings({ ...DEFAULT_SETTINGS, ...data.settings, keys: { ...DEFAULT_SETTINGS.keys, ...(data.settings.keys || {}) } })
  if (data.favorites) localStorage.setItem('oneplayer.favorites', JSON.stringify(data.favorites))
  if (data.volume != null) localStorage.setItem('oneplayer.volume', String(data.volume))
  if (data.muted != null) localStorage.setItem('oneplayer.muted', String(data.muted))
  if (data.playlistUrl) localStorage.setItem('oneplayer.playlistUrl', data.playlistUrl)
  if (data.epgUrl) localStorage.setItem('oneplayer.epgUrl', data.epgUrl)
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
  liveGuide: 'Телепрограмма поверх эфира',
}
