const WINDOW_BACK_MS = 3 * 60 * 60 * 1000
const WINDOW_AHEAD_MS = 30 * 60 * 60 * 1000

export function parseXmltvTime(value) {
  const text = String(value || '').trim()
  const match = text.match(/^(\d{4})(\d{2})(\d{2})(\d{2})(\d{2})(\d{2})(?:\s*([+-])(\d{2})(\d{2}))?/)
  if (!match) return Date.parse(text) || 0
  const zone = match[7] ? `${match[7]}${match[8]}:${match[9]}` : 'Z'
  return Date.parse(`${match[1]}-${match[2]}-${match[3]}T${match[4]}:${match[5]}:${match[6]}${zone}`) || 0
}

export function normalizeEpgKey(name) {
  return String(name || '')
    .toLowerCase()
    .replace(/ё/g, 'е')
    .replace(/\b(uhd|fhd|hd|sd|4k|hevc|hdr|50|60)\b/gi, ' ')
    .replace(/\+\d+/g, ' ')
    .replace(/\([^)]*\)/g, ' ')
    .replace(/[^\p{L}\p{N}]+/gu, ' ')
    .replace(/\s+/g, ' ')
    .trim()
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
  programs[channelId].push({
    id: `${channelId}-${start}`,
    title: decodeXml(block.match(/<title[^>]*>([^<]*)<\/title>/i)?.[1]) || 'Программа',
    start,
    end: stop,
    description: decodeXml(block.match(/<desc[^>]*>([^<]*)<\/desc>/i)?.[1] || ''),
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

export async function parseXmltvBuffer(buffer, now = Date.now()) {
  const from = now - WINDOW_BACK_MS
  const to = now + WINDOW_AHEAD_MS
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
    const keys = [meta.id, ...(meta.names || [])]
    for (const raw of keys) {
      const key = normalizeEpgKey(raw)
      if (key && !nameIndex.has(key)) nameIndex.set(key, meta.id)
    }
  }

  let matched = 0
  const epg = {}
  const channels = playlistChannels.map((channel) => {
    const candidates = [channel.tvgId, channel.tvgName, channel.displayName, channel.name]
      .map((item) => normalizeEpgKey(item))
      .filter(Boolean)

    let epgId = ''
    for (const key of candidates) {
      if (nameIndex.has(key)) {
        epgId = nameIndex.get(key)
        break
      }
    }

    if (!epgId) {
      for (const key of candidates) {
        const words = key.split(' ')
        for (let size = words.length; size > 1; size -= 1) {
          const chunk = words.slice(0, size).join(' ')
          if (nameIndex.has(chunk)) {
            epgId = nameIndex.get(chunk)
            break
          }
        }
        if (epgId) break
      }
    }

    const programs = (epgId && xmltv.programs[epgId]) || []
    if (programs.length) matched += 1
    epg[channel.id] = programs
    if (channel.tvgId) epg[channel.tvgId] = programs
    if (epgId) epg[epgId] = programs

    const icon = xmltv.channels?.[epgId]?.icon || ''
    return {
      ...channel,
      epgId,
      logo: channel.logo || icon,
    }
  })

  return { channels, epg, matched }
}

function preferHttps(url) {
  return url.startsWith('http://') ? `https://${url.slice(7)}` : url
}

async function fetchBinary(url) {
  const targets = [preferHttps(url)]
  if (targets[0] !== url) targets.push(url)

  if (window.oneplayer) {
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

function parseInWorker(buffer) {
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
    worker.postMessage({ buffer }, [buffer])
  })
}

export async function loadXmltv(url) {
  const buffer = await fetchBinary(url)
  try {
    return await parseInWorker(buffer.slice(0))
  } catch {
    return parseXmltvBuffer(buffer)
  }
}
