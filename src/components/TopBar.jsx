import { formatClock } from '../lib/epg.js'
import { useClock } from '../hooks/useClock.js'
import { usePlayer } from '../store/PlayerContext.jsx'

export function TopBar() {
  const now = useClock(1000)
  const { playlistName, channels, setIsModalOpen, status, openMenu } = usePlayer()

  return (
    <header className="flex h-14 shrink-0 items-center justify-between border-b border-line/80 bg-panel/90 px-4">
      <div className="flex items-center gap-3">
        <div className="flex h-8 w-8 items-center justify-center rounded-lg bg-accent text-sm font-bold">▶</div>
        <div>
          <div className="text-[15px] font-semibold tracking-wide">MIREFIR</div>
          <div className="text-[11px] text-white/45">IPTV-плеер</div>
        </div>
      </div>

      <div className="flex items-center gap-4 text-sm">
        <div className="hidden max-w-xs truncate text-white/50 md:block">{status || `${channels.length} каналов`}</div>
        <button
          type="button"
          onClick={openMenu}
          className="remote-hit rounded-lg bg-accent px-3 text-[13px] font-medium"
        >
          Меню
        </button>
        <button
          type="button"
          onClick={() => setIsModalOpen(true)}
          className="rounded-lg border border-white/10 bg-raised px-3 py-1.5 text-[13px] text-white/80 hover:border-accent hover:text-white"
        >
          {playlistName}
        </button>
        <div className="min-w-14 text-right text-lg font-medium tabular-nums">{formatClock(now)}</div>
      </div>
    </header>
  )
}
