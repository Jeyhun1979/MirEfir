import { useRef } from 'react'
import { useHls } from '../../hooks/useHls.js'

export function StreamTile({ channel, muted = true, compact = true, onFocus, onRemove }) {
  const videoRef = useRef(null)
  const { error, loading } = useHls(videoRef, channel?.url || '', { compact })

  return (
    <div className="relative h-full min-h-0 overflow-hidden rounded-xl bg-black" onClick={onFocus}>
      <video ref={videoRef} className="h-full w-full object-contain" muted={muted} playsInline autoPlay />
      {loading ? <div className="absolute inset-0 flex items-center justify-center text-xs text-white/50">Загрузка…</div> : null}
      {error ? <div className="absolute inset-0 flex items-center justify-center px-3 text-center text-xs text-white/60">{error}</div> : null}
      <div className="pointer-events-none absolute inset-x-0 top-0 flex items-center justify-between bg-gradient-to-b from-black/70 p-2 text-xs">
        <span className="truncate">{channel?.displayName}</span>
        <span className={muted ? 'text-white/40' : 'text-live'}>{muted ? 'без звука' : 'звук'}</span>
      </div>
      {onRemove ? (
        <button
          type="button"
          className="absolute right-2 bottom-2 rounded-md bg-black/60 px-2 py-1 text-[11px]"
          onClick={(event) => {
            event.stopPropagation()
            onRemove()
          }}
        >
          Убрать
        </button>
      ) : null}
    </div>
  )
}
