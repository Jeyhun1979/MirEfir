export function isTypingTarget(target) {
  const tag = target?.tagName
  return tag === 'INPUT' || tag === 'TEXTAREA' || Boolean(target?.isContentEditable)
}

export function eventToken(event) {
  const key = event.key
  if (key && key !== 'Unidentified' && key !== 'Process' && key !== 'Dead') return key
  if (event.code && event.code !== 'Unidentified') return event.code
  return `kc:${event.keyCode || event.which || 0}`
}

export function matchesBinding(event, binding) {
  if (!binding) return false
  return binding === event.key || binding === event.code || binding === `kc:${event.keyCode || event.which}`
}

export function isBackKey(event) {
  const { key, keyCode } = event
  return (
    key === 'Escape' ||
    key === 'GoBack' ||
    key === 'BrowserBack' ||
    keyCode === 4 ||
    keyCode === 111 ||
    matchesBinding(event, 'Escape')
  )
}

export function isOkKey(event) {
  const { key, keyCode } = event
  return key === 'Enter' || key === 'NumpadEnter' || key === 'Select' || keyCode === 23 || keyCode === 66
}

export function isMenuKey(event) {
  const { key, keyCode } = event
  return key === 'ContextMenu' || key === 'F2' || key === 'Help' || keyCode === 82 || keyCode === 176
}

export function isSearchKey(event) {
  const { key, keyCode } = event
  return key === 'BrowserSearch' || key === 'Search' || keyCode === 84
}

export function isGuideKey(event) {
  return event.keyCode === 172 || event.keyCode === 165 || event.key === 'F1'
}

export function isMuteKey(event) {
  return event.key === 'AudioVolumeMute' || event.keyCode === 164 || event.key === 'm' || event.key === 'M'
}

export function volumeDelta(event) {
  const { key, code, keyCode } = event
  if (key === 'AudioVolumeUp' || keyCode === 24 || key === '+' || key === '=' || key === 'Add' || code === 'NumpadAdd') return 1
  if (key === 'AudioVolumeDown' || keyCode === 25 || key === '-' || key === '_' || key === 'Subtract' || code === 'NumpadSubtract') return -1
  return 0
}

export function channelDelta(event) {
  if (event.key === 'MediaTrackNext' || event.key === 'ChannelUp' || event.keyCode === 87 || event.keyCode === 166) return 1
  if (event.key === 'MediaTrackPrevious' || event.key === 'ChannelDown' || event.keyCode === 88 || event.keyCode === 167)
    return -1
  return 0
}

export function arrowDir(event) {
  const { key, code, keyCode } = event
  if (key === 'ArrowUp' || key === 'Up' || code === 'ArrowUp' || keyCode === 19) return 'up'
  if (key === 'ArrowDown' || key === 'Down' || code === 'ArrowDown' || keyCode === 20) return 'down'
  if (key === 'ArrowLeft' || key === 'Left' || code === 'ArrowLeft' || keyCode === 21) return 'left'
  if (key === 'ArrowRight' || key === 'Right' || code === 'ArrowRight' || keyCode === 22) return 'right'
  return ''
}

export function keyCaption(binding) {
  if (!binding) return '—'
  if (binding === ' ') return 'OK / пробел'
  if (binding === 'ArrowLeft') return '← Влево'
  if (binding === 'ArrowRight') return '→ Вправо'
  if (binding === 'ArrowUp') return '↑ Вверх'
  if (binding === 'ArrowDown') return '↓ Вниз'
  if (binding === 'Enter') return 'OK / Enter'
  if (binding === 'Escape') return 'Назад'
  if (String(binding).startsWith('kc:')) return `Кнопка пульта ${binding.slice(3)}`
  return binding
}
