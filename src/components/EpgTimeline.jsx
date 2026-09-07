import { useEffect, useMemo, useRef, useState } from 'react'
import { formatClock, getCurrentProgram } from '../lib/epg.js'
import { useClock } from '../hooks/useClock.js'
import { usePlayer } from '../store/PlayerContext.jsx'
import { LogoMark } from './LogoMark.jsx'

const PX_PER_MIN = 3.4
const ROW_H = 44
const HEADER_H = 32
const LABEL_W = 188
const VIEW_H = 268 - HEADER_H
const HOURS = 24

export function EpgTimeline() {
  const now = useClock(30000)
  const { visibleChannels, selectedChannel, selectChannel, getPrograms } = usePlayer()
  const scrollerRef = useRef(null)
  const [scrollTop, setScrollTop] = useState(0)

  const windowStart = useMemo(() => {
    const start = new Date(now)
    start.setMinutes(0, 0, 0)
    start.setHours(start.getHours() - 1)
    return start.getTime()
  }, [now])

  const windowEnd = windowStart + HOURS * 60 * 60 * 1000
  const gridWidth = HOURS * 60 * PX_PER_MIN
  const nowOffset = ((now.getTime() - windowStart) / 60000) * PX_PER_MIN
  const totalHeight = visibleChannels.length * ROW_H

  const ticks = useMemo(() => {
    const list = []
    for (let t = windowStart; t <= windowEnd; t += 30 * 60 * 1000) list.push(t)
    return list
  }, [windowStart, windowEnd])

  const startIndex = Math.max(0, Math.floor(scrollTop / ROW_H) - 2)
  const endIndex = Math.min(visibleChannels.length, startIndex + Math.ceil(VIEW_H / ROW_H) + 6)
  const rows = visibleChannels.slice(startIndex, endIndex)

  useEffect(() => {
    const node = scrollerRef.current
    if (!node) return
    node.scrollLeft = Math.max(0, nowOffset - 160)
  }, [nowOffset, selectedChannel?.id])

  return (
    <footer className="shrink-0 border-t border-line bg-panel" style={{ height: 268 }}>
      <div className="flex h-full">
        <div className="flex shrink-0 flex-col border-r border-line" style={{ width: LABEL_W }}>
          <div
            className="flex items-center px-3 text-[11px] font-semibold uppercase tracking-[0.16em] text-white/35"
            style={{ height: HEADER_H }}
          >
            Программа передач
          </div>
          <div className="relative min-h-0 flex-1 overflow-hidden">
            <div className="absolute left-0 right-0" style={{ top: -scrollTop, height: totalHeight }}>
              {rows.map((channel, offset) => {
                const index = startIndex + offset
                return (
                  <button
                    key={channel.id}
                    type="button"
                    onClick={() => selectChannel(channel.id)}
                    className={`absolute left-0 flex w-full items-center gap-2 px-2 text-left ${
                      channel.id === selectedChannel?.id ? 'bg-accent/15' : ''
                    }`}
                    style={{ top: index * ROW_H, height: ROW_H }}
                  >
                    <LogoMark name={channel.name} logo={channel.logo} size={26} />
                    <span className="truncate text-[12px]">{channel.displayName}</span>
                  </button>
                )
              })}
            </div>
          </div>
        </div>

        <div
          ref={scrollerRef}
          className="scroll-thin min-w-0 flex-1 overflow-auto"
          onScroll={(event) => setScrollTop(event.currentTarget.scrollTop)}
        >
          <div className="relative" style={{ width: gridWidth, height: HEADER_H + totalHeight }}>
            <div
              className="sticky top-0 z-10 flex border-b border-white/5 bg-panel/95 text-[11px] text-white/40"
              style={{ height: HEADER_H }}
            >
              {ticks.map((tick) => (
                <div
                  key={tick}
                  className="shrink-0 border-l border-white/8 pl-2 leading-8"
                  style={{ width: 30 * PX_PER_MIN }}
                >
                  {formatClock(new Date(tick))}
                </div>
              ))}
            </div>

            <div className="relative" style={{ height: totalHeight }}>
              {rows.map((channel, offset) => {
                const index = startIndex + offset
                const programs = getPrograms(channel)
                const current = getCurrentProgram(programs, now.getTime())
                return (
                  <div
                    key={channel.id}
                    className={`absolute left-0 right-0 border-b border-white/5 ${
                      channel.id === selectedChannel?.id ? 'bg-accent/5' : ''
                    }`}
                    style={{ top: index * ROW_H, height: ROW_H }}
                    onClick={() => selectChannel(channel.id)}
                  >
                    {programs
                      .filter((program) => program.end > windowStart && program.start < windowEnd)
                      .map((program) => {
                        const left = ((program.start - windowStart) / 60000) * PX_PER_MIN
                        const width = ((program.end - program.start) / 60000) * PX_PER_MIN
                        const isNow = current?.id === program.id
                        return (
                          <div
                            key={program.id}
                            title={`${program.title}\n${formatClock(new Date(program.start))} – ${formatClock(new Date(program.end))}`}
                            className={`absolute top-1.5 overflow-hidden rounded-md px-2 text-[12px] leading-8 ${
                              isNow ? 'bg-accent text-white shadow-[0_0_12px_rgba(47,124,246,0.35)]' : 'bg-raised text-white/70'
                            }`}
                            style={{ left, width: Math.max(width - 3, 42) }}
                          >
                            <span className="block truncate">{program.title}</span>
                          </div>
                        )
                      })}
                  </div>
                )
              })}
            </div>

            <div
              className="epg-now-line pointer-events-none absolute z-20 w-0.5 bg-live"
              style={{ left: nowOffset, top: HEADER_H, height: totalHeight }}
            />
          </div>
        </div>
      </div>
    </footer>
  )
}
