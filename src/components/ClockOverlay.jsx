import { formatClock } from '../lib/epg.js'
import { useClock } from '../hooks/useClock.js'
import { usePlayer } from '../store/PlayerContext.jsx'

const POS = {
  'top-left': 'top-4 left-4',
  'top-right': 'top-4 right-4',
  'bottom-left': 'bottom-16 left-4',
  'bottom-right': 'bottom-16 right-4',
}

const SIZE = {
  sm: 'text-lg',
  md: 'text-2xl',
  lg: 'text-4xl',
}

export function ClockOverlay() {
  const now = useClock(1000)
  const { settings } = usePlayer()
  if (!settings.clockEnabled) return null

  return (
    <div className={`pointer-events-none absolute z-[45] font-medium tabular-nums text-white/90 drop-shadow ${POS[settings.clockPosition] || POS['top-right']} ${SIZE[settings.clockSize] || SIZE.md}`}>
      {formatClock(now)}
    </div>
  )
}
