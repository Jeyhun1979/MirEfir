function pad(value) {
  return String(value).padStart(2, '0')
}

function toUtcSeconds(ms) {
  return Math.floor(ms / 1000)
}

function dateParts(ms, utc = false) {
  const date = new Date(ms)
  if (utc) {
    return {
      Y: date.getUTCFullYear(),
      m: pad(date.getUTCMonth() + 1),
      d: pad(date.getUTCDate()),
      H: pad(date.getUTCHours()),
      M: pad(date.getUTCMinutes()),
      S: pad(date.getUTCSeconds()),
    }
  }
  return {
    Y: date.getFullYear(),
    m: pad(date.getMonth() + 1),
    d: pad(date.getDate()),
    H: pad(date.getHours()),
    M: pad(date.getMinutes()),
    S: pad(date.getSeconds()),
  }
}

function xtreamStamp(ms, utc = false) {
  const p = dateParts(ms, utc)
  return `${p.Y}-${p.m}-${p.d}:${p.H}-${p.M}`
}

function fillTemplate(template, startMs, endMs) {
  const utc = toUtcSeconds(startMs)
  const lutc = toUtcSeconds(endMs)
  const durationSec = Math.max(1, lutc - utc)
  const durationMin = Math.max(1, Math.round((endMs - startMs) / 60000))
  const offset = Math.max(0, Math.floor((Date.now() - startMs) / 1000))
  const xtream = /timeshift/i.test(template)
  const local = dateParts(startMs, false)
  const zulu = dateParts(startMs, true)
  let text = String(template)
  const put = (token, value) => {
    text = text.replaceAll(token, String(value))
  }
  put('{utc}', utc)
  put('{start}', xtream ? xtreamStamp(startMs) : String(utc))
  put('{lutc}', lutc)
  put('{utcend}', lutc)
  put('{end}', lutc)
  put('{duration}', xtream ? durationMin : durationSec)
  put('{minutes}', durationMin)
  put('{offset}', offset)
  put('${start}', xtreamStamp(startMs))
  put('${timestamp}', String(utc))
  put('${utc}', String(utc))
  put('${duration}', xtream ? String(durationMin) : String(durationSec))
  put('{Y}', local.Y)
  put('{m}', local.m)
  put('{d}', local.d)
  put('{H}', local.H)
  put('{M}', local.M)
  put('{S}', local.S)
  put('{utcY}', zulu.Y)
  put('{utcm}', zulu.m)
  put('{utcd}', zulu.d)
  put('{utcH}', zulu.H)
  put('{utcM}', zulu.M)
  return text
}

function parseXtream(url) {
  const text = String(url || '').split('?')[0]
  const match = text.match(
    /^(https?:\/\/[^/]+)\/(?:live|play)\/([^/]+)\/([^/]+)\/(\d+)(?:\.(m3u8|ts|mkv))?(?:\/(?:index|video)\.(m3u8|ts))?(?:\/)?$/i,
  )
  if (match) return { host: match[1], user: match[2], pass: match[3], id: match[4] }
  const short = text.match(/^(https?:\/\/[^/]+)\/([^/]+)\/([^/]+)\/(\d+)(?:\.(m3u8|ts|mkv))?(?:\/)?$/i)
  if (short && !/^(timeshift|hls|play|live|streaming|xmltv|player_api|panel_api)$/i.test(short[2])) {
    return { host: short[1], user: short[2], pass: short[3], id: short[4] }
  }
  const indexed = text.match(
    /^(https?:\/\/[^/]+)\/([^/]+)\/([^/]+)\/(\d+)\/(?:index|video)\.(m3u8|ts)$/i,
  )
  if (indexed && !/^(timeshift|hls|play|live|streaming|xmltv|player_api|panel_api)$/i.test(indexed[2])) {
    return { host: indexed[1], user: indexed[2], pass: indexed[3], id: indexed[4] }
  }
  return null
}

function xtreamTimeshift(url, startMs, endMs, ext = 'm3u8', utc = false) {
  const parsed = parseXtream(url)
  if (!parsed) return ''
  const duration = Math.max(1, Math.round((endMs - startMs) / 60000))
  const stamp = xtreamStamp(startMs, utc)
  return `${parsed.host}/timeshift/${parsed.user}/${parsed.pass}/${duration}/${stamp}/${parsed.id}.${ext}`
}

function xtreamTimeshiftPhp(url, startMs, endMs, utc = false) {
  const parsed = parseXtream(url)
  if (!parsed) return ''
  const duration = Math.max(1, Math.round((endMs - startMs) / 60000))
  const stamp = encodeURIComponent(xtreamStamp(startMs, utc))
  return `${parsed.host}/streaming/timeshift.php?username=${encodeURIComponent(parsed.user)}&password=${encodeURIComponent(parsed.pass)}&stream=${parsed.id}&start=${stamp}&duration=${duration}`
}

function xtreamTimeshiftUnix(url, startMs, endMs, ext = 'm3u8') {
  const parsed = parseXtream(url)
  if (!parsed) return ''
  const duration = Math.max(1, Math.round((endMs - startMs) / 60000))
  return `${parsed.host}/timeshift/${parsed.user}/${parsed.pass}/${duration}/${toUtcSeconds(startMs)}/${parsed.id}.${ext}`
}

function flussonicTimeshift(url, startMs, endMs) {
  const duration = Math.max(1, toUtcSeconds(endMs) - toUtcSeconds(startMs))
  const utc = toUtcSeconds(startMs)
  if (/\/index\.m3u8/i.test(url)) return url.replace(/\/index\.m3u8.*/i, `/index-${utc}-${duration}.m3u8`)
  if (/\/video\.m3u8/i.test(url)) return url.replace(/\/video\.m3u8.*/i, `/video-${utc}-${duration}.m3u8`)
  if (/index-\d+-\d+/i.test(url)) return url.replace(/index-\d+-\d+/i, `index-${utc}-${duration}`)
  return ''
}

function flussonicAbs(url, startMs) {
  const utc = toUtcSeconds(startMs)
  if (/\/index\.m3u8/i.test(url)) return url.replace(/\/index\.m3u8.*/i, `/timeshift_abs-${utc}.m3u8`)
  return ''
}

function flussonicRel(url, startMs) {
  const offset = Math.max(1, Math.floor((Date.now() - startMs) / 1000))
  if (/\/index\.m3u8/i.test(url)) return url.replace(/\/index\.m3u8.*/i, `/timeshift_rel-${offset}.m3u8`)
  if (/\/video\.m3u8/i.test(url)) return url.replace(/\/video\.m3u8.*/i, `/timeshift_rel-${offset}.m3u8`)
  return ''
}

function catchupSpan(startMs, endMs, now = Date.now()) {
  const start = Number(startMs) || 0
  const rawEnd = endMs && endMs > start ? endMs : start + 60 * 60 * 1000
  const finish = Math.min(rawEnd, now - 1500)
  return { start, finish }
}

function appendUtc(url, startMs, endMs) {
  const clean = String(url).replace(/[?&](utc|lutc|duration)=\d+/gi, '').replace(/\?&/, '?').replace(/[?&]$/, '')
  const join = clean.includes('?') ? '&' : '?'
  return `${clean}${join}utc=${toUtcSeconds(startMs)}&lutc=${toUtcSeconds(endMs)}`
}

export function channelHasCatchup(channel) {
  if (!channel) return false
  if ((Number(channel.catchupDays) || 0) > 0) return true
  if (String(channel.catchupSource || '').trim()) return true
  const type = String(channel.catchupType || channel.catchup || '').trim().toLowerCase()
  return Boolean(type) && !['0', 'false', 'none', 'off', 'no'].includes(type)
}

export function catchupWindowDays(channel, maxDays = 0) {
  if (!channelHasCatchup(channel)) return 0
  const own = Number(channel.catchupDays) || 0
  const cap = Number(maxDays) || 0
  if (own > 0 && cap > 0) return Math.min(own, cap)
  if (own > 0) return own
  return cap > 0 ? cap : 7
}

export function programHasArchive(channel, program, now = Date.now(), maxDays = 0) {
  if (!channelHasCatchup(channel) || !program) return false
  if (program.end > now) return false
  const days = catchupWindowDays(channel, maxDays)
  if (!days) return false
  return program.start >= now - days * 24 * 60 * 60 * 1000
}

export function canPlayArchive(channel, program, now = Date.now(), maxDays = 0) {
  if (!channelHasCatchup(channel) || !program) return false
  if (program.start >= now) return false
  const days = catchupWindowDays(channel, maxDays)
  if (!days) return false
  return program.start >= now - days * 24 * 60 * 60 * 1000
}

export function catchupUrlCandidates(channel, startMs, endMs) {
  if (!channelHasCatchup(channel) || !channel.url || !startMs) return []
  const { start, finish } = catchupSpan(startMs, endMs)
  if (!start || finish <= start) return []
  const type = String(channel.catchupType || channel.catchup || '').toLowerCase()
  const urls = []
  const add = (value) => {
    if (!value || urls.includes(value) || value === channel.url) return
    urls.push(value)
  }

  const xtream = type.includes('xc') || type.includes('default') || type.includes('shift') || type === '' || type.includes('timeshift')
  const indexPlaylist = /\/index\.m3u8/i.test(channel.url)
  const flussonic = type.includes('flussonic') || type.includes('fs') || indexPlaylist || /video\.m3u8/i.test(channel.url)
  const append = type.includes('append') || type.includes('shift') || type.includes('default') || flussonic || !type

  if (indexPlaylist) add(appendUtc(channel.url, start, finish))

  const source = String(channel.catchupSource || '').trim()
  if (source) {
    add(fillTemplate(source, start, finish))
    if (!/^https?:/i.test(source) && source.includes('{')) {
      try {
        add(fillTemplate(new URL(source, channel.url).toString(), start, finish))
      } catch {
        /* relative template may be invalid */
      }
    }
  }

  if (!indexPlaylist && (flussonic || append || source)) add(appendUtc(channel.url, start, finish))
  if (flussonic) {
    add(flussonicTimeshift(channel.url, start, finish))
    add(flussonicAbs(channel.url, start))
    add(flussonicRel(channel.url, start))
  }
  if (xtream || !source) {
    add(xtreamTimeshift(channel.url, start, finish, 'm3u8', false))
    add(xtreamTimeshift(channel.url, start, finish, 'm3u8', true))
    add(xtreamTimeshiftPhp(channel.url, start, finish, false))
    add(xtreamTimeshiftPhp(channel.url, start, finish, true))
    add(xtreamTimeshift(channel.url, start, finish, 'ts', false))
    add(xtreamTimeshiftUnix(channel.url, start, finish, 'm3u8'))
    add(xtreamTimeshiftUnix(channel.url, start, finish, 'ts'))
  }

  return urls.filter((item) => item && item !== channel.url)
}

export function buildCatchupUrl(channel, startMs, endMs) {
  return catchupUrlCandidates(channel, startMs, endMs)[0] || ''
}
