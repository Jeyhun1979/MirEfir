function pad(value) {
  return String(value).padStart(2, '0')
}

function toUtcSeconds(ms) {
  return Math.floor(ms / 1000)
}

function xtreamStamp(ms) {
  const date = new Date(ms)
  return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}:${pad(date.getHours())}-${pad(date.getMinutes())}`
}

function fillTemplate(template, startMs, endMs) {
  const utc = toUtcSeconds(startMs)
  const lutc = toUtcSeconds(endMs)
  const duration = Math.max(1, lutc - utc)
  const offset = Math.max(0, Math.floor((Date.now() - startMs) / 1000))
  return String(template)
    .replaceAll('{utc}', String(utc))
    .replaceAll('{start}', String(utc))
    .replaceAll('{lutc}', String(lutc))
    .replaceAll('{utcend}', String(lutc))
    .replaceAll('{end}', String(lutc))
    .replaceAll('{duration}', String(duration))
    .replaceAll('{offset}', String(offset))
    .replaceAll('${start}', String(utc))
    .replaceAll('${timestamp}', String(utc))
}

function parseXtream(url) {
  const text = String(url || '')
  const match = text.match(
    /^(https?:\/\/[^/]+)\/(?:live|play)\/([^/]+)\/([^/]+)\/(\d+)(?:\.(m3u8|ts|mkv))?(?:\/(?:index|video)\.(m3u8|ts))?(?:\/)?(?:\?.*)?$/i,
  )
  if (match) return { host: match[1], user: match[2], pass: match[3], id: match[4] }
  const short = text.match(/^(https?:\/\/[^/]+)\/([^/]+)\/([^/]+)\/(\d+)(?:\.(m3u8|ts|mkv))?(?:\/)?(?:\?.*)?$/i)
  if (short && !/^(timeshift|hls|play|live)$/i.test(short[2])) {
    return { host: short[1], user: short[2], pass: short[3], id: short[4] }
  }
  return null
}

function xtreamTimeshift(url, startMs, endMs, ext = 'm3u8') {
  const parsed = parseXtream(url)
  if (!parsed) return ''
  const duration = Math.max(1, Math.round((endMs - startMs) / 60000))
  return `${parsed.host}/timeshift/${parsed.user}/${parsed.pass}/${duration}/${xtreamStamp(startMs)}/${parsed.id}.${ext}`
}

function flussonicTimeshift(url, startMs, endMs) {
  const duration = Math.max(1, toUtcSeconds(endMs) - toUtcSeconds(startMs))
  const utc = toUtcSeconds(startMs)
  if (/\/index\.m3u8/i.test(url)) return url.replace(/\/index\.m3u8.*/i, `/index-${utc}-${duration}.m3u8`)
  if (/\/video\.m3u8/i.test(url)) return url.replace(/\/video\.m3u8.*/i, `/video-${utc}-${duration}.m3u8`)
  if (url.includes('index-')) return url.replace(/index-\d+-\d+/, `index-${utc}-${duration}`)
  return ''
}

function appendUtc(url, startMs, endMs) {
  const clean = String(url).replace(/[?&](utc|lutc|duration)=\d+/gi, '').replace(/\?&/, '?').replace(/[?&]$/, '')
  const join = clean.includes('?') ? '&' : '?'
  return `${clean}${join}utc=${toUtcSeconds(startMs)}&lutc=${toUtcSeconds(endMs)}`
}

export function catchupUrlCandidates(channel, startMs, endMs) {
  if (!channel?.url || !startMs) return []
  const finish = endMs && endMs > startMs ? endMs : startMs + 60 * 60 * 1000
  const type = String(channel.catchupType || channel.catchup || '').toLowerCase()
  const urls = []
  const add = (value) => {
    if (value && !urls.includes(value)) urls.push(value)
  }

  if (channel.catchupSource) add(fillTemplate(channel.catchupSource, startMs, finish))
  if (type.includes('flussonic')) add(flussonicTimeshift(channel.url, startMs, finish))
  add(xtreamTimeshift(channel.url, startMs, finish, 'm3u8'))
  add(xtreamTimeshift(channel.url, startMs, finish, 'ts'))
  add(flussonicTimeshift(channel.url, startMs, finish))
  add(appendUtc(channel.url, startMs, finish))
  return urls
}

export function buildCatchupUrl(channel, startMs, endMs) {
  return catchupUrlCandidates(channel, startMs, endMs)[0] || ''
}

export function canPlayArchive(channel, program, now = Date.now(), extraDays = 0) {
  if (!channel || !program) return false
  if (program.start >= now) return false
  const days = Math.max(Number(channel.catchupDays) || 0, Number(extraDays) || 0, 0)
  if (!days) return Boolean(channel.catchup || channel.catchupSource || channel.catchupType)
  const oldest = now - days * 24 * 60 * 60 * 1000
  return program.start >= oldest
}
