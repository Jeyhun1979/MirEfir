export function getCurrentProgram(programs, now = Date.now()) {
  if (!programs?.length) return null
  return programs.find((item) => item.start <= now && now < item.end) || null
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
