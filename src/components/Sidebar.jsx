import { useEffect, useRef } from 'react'
import { usePlayer } from '../store/PlayerContext.jsx'

export function Sidebar() {
  const { groups, selectedGroupId, selectGroup, focusZone, setFocusZone, listMode } = usePlayer()
  const listRef = useRef(null)

  useEffect(() => {
    const active = listRef.current?.querySelector('[data-active="true"]')
    active?.scrollIntoView({ block: 'nearest' })
  }, [selectedGroupId, focusZone, listMode])

  return (
    <aside
      className={`flex w-[220px] shrink-0 flex-col border-r border-line bg-panel ${
        focusZone === 'groups' ? 'ring-1 ring-accent/40' : ''
      }`}
      onClick={() => setFocusZone('groups')}
    >
      <div className="px-4 py-3 text-[11px] font-semibold uppercase tracking-[0.18em] text-white/35">
        Категории
      </div>
      <div ref={listRef} className="scroll-thin flex-1 overflow-y-auto px-2 pb-3">
        {groups.map((group, index) => {
          const active = listMode === 'live' && group.id === (selectedGroupId || 'all')
          const focused = focusZone === 'groups' && active
          return (
            <div key={group.id}>
              {index === 3 ? (
                <div className="mx-2 mb-2 mt-1 border-t border-white/8 pt-2 text-[10px] uppercase tracking-[0.16em] text-white/25">
                  Из плейлиста
                </div>
              ) : null}
              <button
                type="button"
                data-active={active}
                onClick={() => selectGroup(group.id)}
                className={`remote-hit mb-1 flex w-full items-center justify-between rounded-lg px-3 text-left text-[13px] transition ${
                  focused ? 'focus-tile bg-accent text-white' : active ? 'bg-white/8 text-white' : 'text-white/65 hover:bg-white/5'
                }`}
              >
                <span className="truncate">{group.name}</span>
                <span className={`text-[11px] ${focused ? 'text-white/80' : 'text-white/30'}`}>{group.count}</span>
              </button>
            </div>
          )
        })}
      </div>
    </aside>
  )
}
