export function getCurrentProgram(programs, now = Date.now()) {
  if (!programs?.length) return null
  let best = null
  for (const item of programs) {
    if (item.start <= now && now < item.end && (!best || item.start > best.start)) best = item
  }
  return best
}

export function getNextProgram(programs, now = Date.now()) {
  if (!programs?.length) return null
  const current = getCurrentProgram(programs, now)
  if (current) return programs.find((item) => item.start >= current.end) || null
  return programs.find((item) => item.start > now) || null
}

export function getProgramProgress(program, now = Date.now()) {
  if (!program) return 0
  const span = program.end - program.start
  if (span <= 0) return 0
  return Math.min(1, Math.max(0, (now - program.start) / span))
}

export function formatClock(date) {
  return date.toLocaleTimeString('ru-RU', { hour: '2-digit', minute: '2-digit' })
}

export function formatRange(start, end) {
  const opts = { hour: '2-digit', minute: '2-digit' }
  return `${new Date(start).toLocaleTimeString('ru-RU', opts)} – ${new Date(end).toLocaleTimeString('ru-RU', opts)}`
}

export function formatRemaining(program, now = Date.now()) {
  if (!program) return ''
  const minutes = Math.max(0, Math.round((program.end - now) / 60000))
  if (minutes < 60) return `${minutes} мин`
  const hours = Math.floor(minutes / 60)
  const rest = minutes % 60
  return rest ? `${hours} ч ${rest} мин` : `${hours} ч`
}

export function startOfDay(ts = Date.now()) {
  const date = new Date(ts)
  date.setHours(0, 0, 0, 0)
  return date.getTime()
}

export function formatDayParts(ts) {
  const date = new Date(ts)
  return {
    day: String(date.getDate()),
    month: date.toLocaleDateString('ru-RU', { month: 'short' }).replace('.', '').replace(' ', ''),
    week: date.toLocaleDateString('ru-RU', { weekday: 'short' }).replace('.', '').toUpperCase(),
  }
}

export function formatDayShort(ts) {
  const { day, month, week } = formatDayParts(ts)
  return `${day}${month} ${week}`
}

export function collectProgramDays(programs, now = Date.now()) {
  const days = new Set()
  for (const item of programs || []) days.add(startOfDay(item.start))
  if (!days.size) days.add(startOfDay(now))
  return [...days].sort((a, b) => a - b)
}

export function formatDayLong(ts) {
  const date = new Date(ts)
  const week = date.toLocaleDateString('ru-RU', { weekday: 'long' })
  const month = date.toLocaleDateString('ru-RU', { month: 'long' })
  const text = `${week}, ${date.getDate()} ${month}`
  return text.charAt(0).toUpperCase() + text.slice(1)
}

export function collectGuideDays({ programs, now = Date.now(), archiveDays = 0, archiveEnabled = false, catchupDays = 0, epgDays = 7 }) {
  const today = startOfDay(now)
  const dayMs = 24 * 60 * 60 * 1000
  const past = Math.max(Number(epgDays) || 0, archiveEnabled ? Number(archiveDays) || 0 : 0, Number(catchupDays) || 0, 1)
  const future = Math.max(Number(epgDays) || 0, 7)
  const from = today - past * dayMs
  const to = today + future * dayMs
  const days = new Set([today])
  for (let i = 1; i <= past; i += 1) days.add(today - i * dayMs)
  for (let i = 1; i <= future; i += 1) days.add(today + i * dayMs)
  for (const item of programs || []) {
    const day = startOfDay(item.start)
    if (day >= from && day <= to) days.add(day)
  }
  return [...days].sort((a, b) => a - b)
}

export function programsOnDay(programs, day) {
  const from = startOfDay(day)
  const to = from + 24 * 60 * 60 * 1000
  return (programs || []).filter((item) => item.start < to && item.end > from)
}
