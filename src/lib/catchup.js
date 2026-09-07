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

function xtreamTimeshift(url, startMs, endMs) {
  const match = String(url).match(/^(https?:\/\/[^/]+)\/(?:live\/)?([^/]+)\/([^/]+)\/(\d+)\.(m3u8|ts|mkv)/i)
  if (!match) return ''
  const [, host, user, pass, id] = match
  const duration = Math.max(1, toUtcSeconds(endMs) - toUtcSeconds(startMs))
  return `${host}/timeshift/${user}/${pass}/${duration}/${xtreamStamp(startMs)}/${id}.m3u8`
}

function flussonicTimeshift(url, startMs, endMs) {
  const duration = Math.max(1, toUtcSeconds(endMs) - toUtcSeconds(startMs))
  const utc = toUtcSeconds(startMs)
  if (/\/index\.m3u8/i.test(url)) return url.replace(/\/index\.m3u8.*/i, `/index-${utc}-${duration}.m3u8`)
  if (/\/video\.m3u8/i.test(url)) return url.replace(/\/video\.m3u8.*/i, `/video-${utc}-${duration}.m3u8`)
  if (url.includes('index-')) return url.replace(/index-\d+-\d+/, `index-${utc}-${duration}`)
  return ''
}

export function buildCatchupUrl(channel, startMs, endMs) {
  if (!channel?.url || !startMs) return ''
  const finish = endMs && endMs > startMs ? endMs : startMs + 60 * 60 * 1000
  const type = String(channel.catchupType || channel.catchup || '').toLowerCase()
  if (channel.catchupSource) return fillTemplate(channel.catchupSource, startMs, finish)

  if (type.includes('flussonic')) return flussonicTimeshift(channel.url, startMs, finish) || appendUtc(channel.url, startMs, finish)
  if (type === 'xc' || type === 'xtream' || type.includes('shift') || /\/live\//i.test(channel.url)) {
    return xtreamTimeshift(channel.url, startMs, finish) || appendUtc(channel.url, startMs, finish)
  }
  if (type === 'append' || type === 'default' || !type) return appendUtc(channel.url, startMs, finish)
  return appendUtc(channel.url, startMs, finish)
}

function appendUtc(url, startMs, endMs) {
  const join = url.includes('?') ? '&' : '?'
  return `${url}${join}utc=${toUtcSeconds(startMs)}&lutc=${toUtcSeconds(endMs)}`
}

export function canPlayArchive(channel, program, now = Date.now(), extraDays = 0) {
  if (!channel || !program) return false
  if (program.start >= now) return false
  const days = Math.max(Number(channel.catchupDays) || 0, Number(extraDays) || 0, 0)
  if (!days) return Boolean(channel.catchup || channel.catchupSource || channel.catchupType)
  const oldest = now - days * 24 * 60 * 60 * 1000
  return program.start >= oldest
}
