const DAY_MS = 24 * 60 * 60 * 1000
const DEFAULT_BACK_DAYS = 7
const DEFAULT_AHEAD_DAYS = 7
const DESC_LIMIT = 1600

export function parseXmltvTime(value) {
  const text = String(value || '').trim()
  const match = text.match(/^(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})(?:\s*([+-])(\d{2})(\d{2}))?/)
  if (!match) return Date.parse(text) || 0
  if (match[7]) {
    return Date.parse(`${match[1]}-${match[2]}-${match[3]}T${match[4]}:${match[5]}:${match[6]}${match[7]}${match[8]}:${match[9]}`) || 0
  }
  return new Date(
    Number(match[1]),
    Number(match[2]) - 1,
    Number(match[3]),
    Number(match[4]),
    Number(match[5]),
    Number(match[6]),
  ).getTime() || 0
}

export function normalizeEpgKey(name) {
  return foldEpgKey(name)
}

function foldEpgKey(name, { keepShift = false, keepParen = false } = {}) {
  let text = String(name || '')
    .toLowerCase()
    .replace(/ё/g, 'е')
  if (!keepShift) text = text.replace(/\+\d+/g, ' ')
  if (!keepParen) text = text.replace(/\([^)]*\)/g, ' ')
  else text = text.replace(/[()]/g, ' ')
  return text
    .replace(/\b(uhd|fhd|hd|sd|4k|hevc|hdr|50|60)\b/gi, ' ')
    .replace(keepShift ? /[^\p{L}\p{N}+]+/gu : /[^\p{L}\p{N}]+/gu, ' ')
    .replace(/\s+/g, ' ')
    .trim()
}

function isShiftedEpgName(name) {
  return /\+\d+/.test(String(name || ''))
}

function channelShiftRank(meta) {
  return (meta?.names || []).some(isShiftedEpgName) ? 2 : 0
}

function putEpgIndex(index, key, id, rank) {
  if (!key) return
  const prev = index.get(key)
  if (!prev || rank < prev.rank) index.set(key, { id, rank })
}

function lookupEpgIndex(index, keys) {
  for (const key of keys) {
    if (key && index.has(key)) return index.get(key).id
  }
  return ''
}

function uniqueEpgIds(index, keys) {
  const ids = []
  const seen = new Set()
  for (const key of keys) {
    if (!key || !index.has(key)) continue
    const id = index.get(key).id
    if (seen.has(id)) continue
    seen.add(id)
    ids.push(id)
  }
  return ids
}

function preferCanonicalEpg(xmltv, ids) {
  const list = ids.filter(Boolean)
  return list.find((id) => channelShiftRank(xmltv.channels?.[id]) === 0) || list[0] || ''
}

function candidateKeys(value) {
  const raw = String(value || '').trim()
  if (!raw) return []
  return [
    raw.toLowerCase(),
    foldEpgKey(raw, { keepShift: true, keepParen: true }),
    foldEpgKey(raw, { keepShift: true }),
    foldEpgKey(raw),
  ]
}

function prefixFallback(index, keys) {
  for (const key of keys) {
    const shifted = /\+\d+/.test(key)
    const words = String(key || '')
      .split(' ')
      .filter(Boolean)
    for (let size = words.length; size > 1; size -= 1) {
      const chunk = words.slice(0, size).join(' ')
      if (shifted && !/\+\d+/.test(chunk)) continue
      if (index.has(chunk)) return index.get(chunk).id
    }
  }
  return ''
}

function decodeXml(value) {
  return String(value || '')
    .replace(/&amp;/g, '&')
    .replace(/&quot;/g, '"')
    .replace(/&#39;|&apos;/g, "'")
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .trim()
}

function attr(block, name) {
  const match = block.match(new RegExp(`${name}="([^"]*)"`))
  return match ? decodeXml(match[1]) : ''
}

function isGzip(buffer) {
  const bytes = new Uint8Array(buffer)
  return bytes.length > 2 && bytes[0] === 0x1f && bytes[1] === 0x8b
}

function ingestBlock(block, isChannel, channels, programs, from, to) {
  if (isChannel) {
    const id = attr(block, 'id')
    if (!id) return
    const names = [...block.matchAll(/<display-name[^>]*>([^<]*)<\/display-name>/gi)].map((item) => decodeXml(item[1]))
    const icon = block.match(/<icon[^>]+src="([^"]+)"/i)?.[1] || ''
    channels[id] = { id, names: names.filter(Boolean), icon: decodeXml(icon) }
    return
  }

  const channelId = attr(block, 'channel')
  const start = parseXmltvTime(attr(block, 'start'))
  const stop = parseXmltvTime(attr(block, 'stop'))
  if (!channelId || !start || !stop) return
  if (stop < from || start > to) return

  if (!programs[channelId]) programs[channelId] = []
  let description = decodeXml(block.match(/<desc[^>]*>([^<]*)<\/desc>/i)?.[1] || '')
  if (description.length > DESC_LIMIT) description = `${description.slice(0, DESC_LIMIT).replace(/\s+\S*$/, '')}…`
  programs[channelId].push({
    id: `${channelId}-${start}`,
    title: decodeXml(block.match(/<title[^>]*>([^<]*)<\/title>/i)?.[1]) || 'Программа',
    start,
    end: stop,
    description,
  })
}

function consumeChunk(xml, channels, programs, from, to) {
  let cursor = 0

  while (cursor < xml.length) {
    const channelAt = xml.indexOf('<channel', cursor)
    const programmeAt = xml.indexOf('<programme', cursor)
    if (channelAt < 0 && programmeAt < 0) return ''

    const next = channelAt < 0 ? programmeAt : programmeAt < 0 ? channelAt : Math.min(channelAt, programmeAt)
    const isChannel = next === channelAt
    const endTag = isChannel ? '</channel>' : '</programme>'
    const end = xml.indexOf(endTag, next)
    if (end < 0) return xml.slice(next)

    ingestBlock(xml.slice(next, end + endTag.length), isChannel, channels, programs, from, to)
    cursor = end + endTag.length
  }

  return ''
}

export async function parseXmltvBuffer(buffer, now = Date.now(), options = {}) {
  const backDays = Math.max(1, Number(options.backDays) || DEFAULT_BACK_DAYS)
  const aheadDays = Math.max(1, Number(options.aheadDays) || DEFAULT_AHEAD_DAYS)
  const from = now - backDays * DAY_MS
  const to = now + aheadDays * DAY_MS
  const channels = {}
  const programs = {}

  let stream = new Blob([buffer]).stream()
  if (isGzip(buffer)) {
    if (typeof DecompressionStream !== 'function') {
      throw new Error('Браузер не умеет распаковывать .xml.gz')
    }
    stream = stream.pipeThrough(new DecompressionStream('gzip'))
  }

  const reader = stream.getReader()
  const decoder = new TextDecoder()
  let leftover = ''
  let sawXmltv = false

  while (true) {
    const { done, value } = await reader.read()
    leftover += decoder.decode(value || new Uint8Array(), { stream: !done })
    if (!sawXmltv && (leftover.includes('<tv') || leftover.includes('<programme') || leftover.includes('<channel'))) {
      sawXmltv = true
    }
    leftover = consumeChunk(leftover, channels, programs, from, to)
    if (done) break
  }

  if (!sawXmltv && !Object.keys(channels).length) {
    throw new Error('Файл не похож на XMLTV')
  }

  for (const list of Object.values(programs)) {
    list.sort((a, b) => a.start - b.start)
  }

  return { channels, programs }
}

export function bindEpgToChannels(playlistChannels, xmltv) {
  const nameIndex = new Map()

  for (const meta of Object.values(xmltv.channels || {})) {
    const rank = channelShiftRank(meta)
    putEpgIndex(nameIndex, String(meta.id || '').toLowerCase(), meta.id, 0)
    for (const raw of meta.names || []) {
      putEpgIndex(nameIndex, foldEpgKey(raw, { keepShift: true, keepParen: true }), meta.id, rank)
      putEpgIndex(nameIndex, foldEpgKey(raw, { keepShift: true }), meta.id, rank)
      putEpgIndex(nameIndex, foldEpgKey(raw), meta.id, rank)
    }
  }

  let matched = 0
  const epg = {}
  const channels = playlistChannels.map((channel) => {
    const idKeys = candidateKeys(channel.tvgId)
    const nameKeys = [
      ...candidateKeys(channel.tvgName),
      ...candidateKeys(channel.displayName),
      ...candidateKeys(channel.name),
    ]
    const playlistShifted = [channel.tvgName, channel.displayName, channel.name].some(isShiftedEpgName)
    let epgId = playlistShifted
      ? lookupEpgIndex(nameIndex, [...nameKeys, ...idKeys])
      : preferCanonicalEpg(xmltv, [...uniqueEpgIds(nameIndex, idKeys), ...uniqueEpgIds(nameIndex, nameKeys)])
    if (!epgId) epgId = prefixFallback(nameIndex, [...nameKeys, ...idKeys])

    const programs = (epgId && xmltv.programs[epgId]) || []
    if (programs.length) {
      matched += 1
      epg[channel.id] = programs
    }

    const icon = xmltv.channels?.[epgId]?.icon || ''
    return {
      ...channel,
      epgId,
      logo: channel.logo || icon,
    }
  })

  return { channels, epg, matched }
}

export function slimXmltv(xmltv, playlistChannels) {
  const keep = new Set()
  for (const channel of playlistChannels || []) {
    if (channel?.epgId) keep.add(channel.epgId)
  }
  const programs = {}
  for (const id of keep) {
    if (xmltv?.programs?.[id]) programs[id] = xmltv.programs[id]
  }
  return { channels: xmltv?.channels || {}, programs }
}

function preferHttps(url) {
  return url.startsWith('http://') ? `https://${url.slice(7)}` : url
}

async function fetchBinary(url) {
  const targets = [preferHttps(url)]
  if (targets[0] !== url) targets.push(url)

  if (window.mirefir) {
    for (const target of targets) {
      try {
        const direct = await fetch(target, { cache: 'no-store', redirect: 'follow' })
        if (direct.ok) return direct.arrayBuffer()
      } catch {
        /* CORS or mixed content */
      }
    }
  }

  for (const target of targets) {
    try {
      const proxied = await fetch(`/api/fetch?url=${encodeURIComponent(target)}`, { cache: 'no-store' })
      if (proxied.ok) return proxied.arrayBuffer()
    } catch {
      /* try next */
    }
  }

  throw new Error('Не удалось скачать EPG. Проверьте ссылку или откройте Electron.')
}

function parseInWorker(buffer, options) {
  return new Promise((resolve, reject) => {
    const worker = new Worker(new URL('../workers/xmltvWorker.js', import.meta.url), { type: 'module' })
    const timer = window.setTimeout(() => {
      worker.terminate()
      reject(new Error('Разбор EPG занял слишком много времени'))
    }, 180000)

    worker.onmessage = (event) => {
      window.clearTimeout(timer)
      worker.terminate()
      if (event.data?.ok) resolve(event.data.payload)
      else reject(new Error(event.data?.error || 'Ошибка разбора XMLTV'))
    }
    worker.onerror = (error) => {
      window.clearTimeout(timer)
      worker.terminate()
      reject(error)
    }
    worker.postMessage({ buffer, options }, [buffer])
  })
}

export function mergeXmltv(parts) {
  const channels = {}
  const programs = {}
  for (const xmltv of parts || []) {
    Object.assign(channels, xmltv?.channels || {})
    for (const [id, list] of Object.entries(xmltv?.programs || {})) {
      if (!programs[id]) programs[id] = []
      programs[id].push(...list)
    }
  }
  for (const [id, list] of Object.entries(programs)) {
    const seen = new Set()
    const next = []
    for (const item of list.sort((a, b) => a.start - b.start)) {
      const key = `${item.start}-${item.end}-${item.title}`
      if (seen.has(key)) continue
      seen.add(key)
      next.push(item)
    }
    programs[id] = next
  }
  return { channels, programs }
}

export async function loadXmltv(url, options = {}) {
  const buffer = await fetchBinary(url)
  return parseInWorker(buffer.slice(0), options)
}
