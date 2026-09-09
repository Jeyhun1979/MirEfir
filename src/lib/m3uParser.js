import { attachLogos } from './channelLogos.js'

const ATTR_RE = /([A-Za-z0-9_-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s,]+))/g
const EMPTY_LOGO = /^(n\/?a|null|undefined|none|-)?$/i

function hash(value) {
  let h = 0
  for (let i = 0; i < value.length; i += 1) {
    h = (h << 5) - h + value.charCodeAt(i)
    h |= 0
  }
  return Math.abs(h).toString(36)
}

function decodeEntities(value) {
  return String(value || '')
    .replace(/&amp;/g, '&')
    .replace(/&quot;/g, '"')
    .replace(/&#39;|&apos;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
}

export function extractExtInfTags(line) {
  const attrs = {}
  ATTR_RE.lastIndex = 0
  let match = ATTR_RE.exec(line)
  while (match) {
    const key = match[1].toLowerCase()
    const raw = match[2] ?? match[3] ?? match[4] ?? ''
    attrs[key] = decodeEntities(raw).trim()
    match = ATTR_RE.exec(line)
  }
  return attrs
}

export function normalizeLogo(url) {
  const logo = decodeEntities(url || '').trim()
  if (!logo || EMPTY_LOGO.test(logo)) return ''
  if (logo.startsWith('//')) return `https:${logo}`
  return logo
}

export function normalizeGroup(name) {
  const group = decodeEntities(name || '').trim()
  if (!group) return 'Другие'
  return group.charAt(0).toUpperCase() + group.slice(1)
}

function parseCatchupDays(attrs) {
  const rec = Number.parseInt(attrs['tvg-rec'] || attrs['catchup-days'] || '', 10)
  if (Number.isFinite(rec) && rec > 0 && rec <= 40) return rec
  const shift = Number.parseInt(attrs.timeshift || '', 10)
  if (Number.isFinite(shift) && shift > 0 && shift <= 40) return shift
  return 0
}

function parseExtInf(line) {
  const comma = line.lastIndexOf(',')
  const meta = comma >= 0 ? line.slice(0, comma) : line
  const title = (comma >= 0 ? line.slice(comma + 1) : 'Без названия').trim()
  const attrs = extractExtInfTags(meta)
  const duration = Number.parseFloat(meta.replace('#EXTINF:', '').split(/\s+/)[0])
  const displayName = decodeEntities(title) || attrs['tvg-name'] || 'Без названия'

  return {
    name: attrs['tvg-name'] || displayName,
    displayName,
    logo: normalizeLogo(attrs['tvg-logo'] || attrs.logo || attrs['tvg_logo']),
    group: normalizeGroup(attrs['group-title'] || attrs.group || attrs['group-name']),
    tvgId: attrs['tvg-id'] || '',
    tvgName: attrs['tvg-name'] || displayName,
    number: Number.parseInt(attrs['tvg-chno'] || attrs['channel-id'] || attrs['tvg-chno'], 10) || 0,
    duration: Number.isFinite(duration) ? duration : -1,
    catchup: attrs.catchup || attrs['catchup-type'] || '',
    catchupType: attrs['catchup-type'] || attrs.catchup || '',
    catchupSource: attrs['catchup-source'] || '',
    catchupDays: parseCatchupDays(attrs),
    url: '',
    id: '',
  }
}

export function groupChannelsByCategory(channels) {
  const counts = new Map()
  for (const channel of channels) {
    const group = normalizeGroup(channel.group)
    channel.group = group
    counts.set(group, (counts.get(group) || 0) + 1)
  }

  return [...counts.entries()].map(([name, count]) => ({
    id: name,
    name,
    count,
  }))
}

export function parseM3U(text, sourceName = 'Плейлист') {
  if (!text || typeof text !== 'string') {
    throw new Error('Плейлист пуст или повреждён')
  }

  const lines = text.replace(/^\uFEFF/, '').split(/\r?\n/)
  const channels = []
  const epgUrls = new Set()
  let pending = null
  let lastGroup = ''
  let playlistName = sourceName.replace(/\.(m3u8?|txt)$/i, '')
  let playlistCatchup = { type: '', days: 0, source: '' }

  for (const raw of lines) {
    const line = raw.trim()
    if (!line) continue

    if (line.startsWith('#EXTM3U')) {
      const attrs = extractExtInfTags(line)
      playlistCatchup = {
        type: attrs.catchup || attrs['catchup-type'] || '',
        days: parseCatchupDays(attrs),
        source: attrs['catchup-source'] || '',
      }
      for (const key of ['url-tvg', 'x-tvg-url', 'tvg-url']) {
        if (!attrs[key]) continue
        attrs[key]
          .split(',')
          .map((item) => item.trim())
          .filter(Boolean)
          .forEach((url) => epgUrls.add(url))
      }
      continue
    }

    if (line.startsWith('#PLAYLIST:')) {
      playlistName = decodeEntities(line.slice(10).trim()) || playlistName
      continue
    }

    if (line.startsWith('#EXTGRP:')) {
      lastGroup = normalizeGroup(line.slice(8))
      if (pending) pending.group = lastGroup
      continue
    }

    if (line.startsWith('#EXTINF:')) {
      pending = parseExtInf(line)
      if (pending.group === 'Другие' && lastGroup) pending.group = lastGroup
      continue
    }

    if (line.startsWith('#')) continue

    if (pending) {
      pending.url = line
      pending.id = `${channels.length + 1}-${hash(line + pending.name)}`
      if (!pending.number) pending.number = channels.length + 1
      if (!pending.catchup && playlistCatchup.type) {
        pending.catchup = playlistCatchup.type
        pending.catchupType = pending.catchupType || playlistCatchup.type
      }
      if (!pending.catchupSource && playlistCatchup.source) pending.catchupSource = playlistCatchup.source
      if (!pending.catchupDays && pending.catchup && playlistCatchup.days) pending.catchupDays = playlistCatchup.days
      channels.push(pending)
      lastGroup = pending.group
      pending = null
    }
  }

  if (!channels.length) {
    throw new Error('В файле нет каналов. Нужен M3U с строками #EXTINF и URL.')
  }

  const withLogos = attachLogos(channels)

  return {
    name: playlistName,
    channels: withLogos,
    groups: groupChannelsByCategory(withLogos),
    epgUrls: [...epgUrls],
  }
}

async function fetchPlaylistText(url) {
  try {
    const direct = await fetch(url, { cache: 'no-store' })
    if (direct.ok) return direct.text()
  } catch {
    /* CORS in the browser — go through the local Vite proxy */
  }

  const proxied = await fetch(`/api/playlist?url=${encodeURIComponent(url)}`, { cache: 'no-store' })
  if (!proxied.ok) {
    throw new Error(`Не удалось загрузить плейлист (${proxied.status})`)
  }
  return proxied.text()
}

export async function loadPlaylistFromUrl(url) {
  const text = await fetchPlaylistText(url)
  let name = decodeURIComponent(url.split('/').pop() || 'Плейлист')
  if (/^playlist\.m3u8?$/i.test(name)) {
    try {
      name = new URL(url).hostname.replace(/^www\./, '')
    } catch {
      name = 'Мой плейлист'
    }
  }
  const parsed = parseM3U(text, name)
  parsed.rawText = text
  return parsed
}

export async function loadPlaylistFromFile(file) {
  const text = await file.text()
  return parseM3U(text, file.name)
}

export const collectGroups = groupChannelsByCategory
