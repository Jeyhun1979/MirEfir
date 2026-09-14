import { useEffect, useRef, useState } from 'react'
import { isBackKey } from '../lib/remoteKeys.js'
import { usePlayer } from '../store/PlayerContext.jsx'

const electron = typeof window !== 'undefined' && window.mirefir?.platform === 'electron'
const EDGE = 72

export function WindowChrome() {
  const { uiScreen, liveGuideOpen, isModalOpen, exitPrompt, channelMenu, osFullscreen, padOpen } = usePlayer()
  const [peek, setPeek] = useState(true)
  const [topScrim, setTopScrim] = useState(true)
  const [bottomScrim, setBottomScrim] = useState(true)
  const hideTimer = useRef(0)
  const bottomTimer = useRef(0)
  const barRef = useRef(null)

  useEffect(() => {
    if (!electron) return undefined
    const showTop = () => {
      setPeek(true)
      setTopScrim(false)
      window.clearTimeout(hideTimer.current)
      if (!osFullscreen) return
      hideTimer.current = window.setTimeout(() => {
        setPeek(false)
        setTopScrim(true)
      }, 1400)
    }
    const showBottom = () => {
      setBottomScrim(false)
      window.clearTimeout(bottomTimer.current)
      if (!osFullscreen) return
      bottomTimer.current = window.setTimeout(() => setBottomScrim(true), 1400)
    }
    const onMove = (event) => {
      if (!osFullscreen) {
        setPeek(true)
        setTopScrim(false)
        setBottomScrim(false)
        return
      }
      const height = window.innerHeight || 0
      const overBar = barRef.current?.contains(event.target)
      if (event.clientY <= EDGE || overBar) showTop()
      if (height && event.clientY >= height - EDGE) showBottom()
    }
    if (osFullscreen) {
      setPeek(false)
      setTopScrim(true)
      setBottomScrim(true)
    } else {
      setPeek(true)
      setTopScrim(false)
      setBottomScrim(false)
    }
    window.addEventListener('mousemove', onMove)
    return () => {
      window.removeEventListener('mousemove', onMove)
      window.clearTimeout(hideTimer.current)
      window.clearTimeout(bottomTimer.current)
    }
  }, [osFullscreen])

  useEffect(() => {
    if (!electron) return undefined
    const overlay = uiScreen || liveGuideOpen || isModalOpen || exitPrompt || channelMenu || padOpen
    const onKey = (event) => {
      if (event.key === 'F11') {
        event.preventDefault()
        event.stopImmediatePropagation()
        window.mirefir.toggleFullscreen?.()
        return
      }
      if (!isBackKey(event) || overlay || !osFullscreen) return
      event.preventDefault()
      event.stopImmediatePropagation()
      window.mirefir.setFullscreen?.(false)
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [channelMenu, exitPrompt, isModalOpen, liveGuideOpen, osFullscreen, padOpen, uiScreen])

  if (!electron) return null

  const hidden = osFullscreen && !peek

  return (
    <>
      {osFullscreen ? (
        <>
          <div
            className={`pointer-events-none fixed inset-x-0 top-0 z-[80] h-28 bg-gradient-to-b from-black via-black/75 to-transparent transition-opacity duration-200 ${
              topScrim ? 'opacity-100' : 'opacity-0'
            }`}
          />
          <div
            className={`pointer-events-none fixed inset-x-0 bottom-0 z-[80] h-32 bg-gradient-to-t from-black via-black/80 to-transparent transition-opacity duration-200 ${
              bottomScrim ? 'opacity-100' : 'opacity-0'
            }`}
          />
        </>
      ) : null}
      <div
        ref={barRef}
        className={`window-chrome z-[90] flex h-9 shrink-0 items-center justify-between bg-[#0b0e14] text-white/80 ${
          osFullscreen ? 'fixed inset-x-0 top-0' : 'relative'
        } ${hidden ? 'pointer-events-none -translate-y-full opacity-0' : 'translate-y-0 opacity-100'} transition-transform duration-150`}
      >
        <div className="window-chrome-drag flex min-w-0 flex-1 items-center gap-2 px-3 text-[13px] font-medium">
          <span className="flex h-5 w-5 items-center justify-center rounded bg-accent text-[10px] font-bold text-white">▶</span>
          <span>MirEfir</span>
        </div>
        <div className="window-chrome-controls flex h-full">
          <button type="button" title="Свернуть" className="window-chrome-btn" onClick={() => window.mirefir.minimize?.()}>
            <svg viewBox="0 0 12 12" className="h-3 w-3" fill="currentColor" aria-hidden="true">
              <rect x="1" y="5.5" width="10" height="1.2" rx="0.4" />
            </svg>
          </button>
          <button
            type="button"
            title={osFullscreen ? 'Выйти из полного экрана' : 'На весь экран'}
            className="window-chrome-btn"
            onClick={() => window.mirefir.toggleFullscreen?.()}
          >
            {osFullscreen ? (
              <svg viewBox="0 0 12 12" className="h-3 w-3" fill="none" stroke="currentColor" strokeWidth="1.3" aria-hidden="true">
                <path d="M2 4.5V2h2.5M10 7.5V10H7.5M2 7.5V10h2.5M10 4.5V2H7.5" />
              </svg>
            ) : (
              <svg viewBox="0 0 12 12" className="h-3 w-3" fill="none" stroke="currentColor" strokeWidth="1.3" aria-hidden="true">
                <rect x="1.6" y="1.6" width="8.8" height="8.8" rx="0.6" />
              </svg>
            )}
          </button>
          <button type="button" title="Закрыть" className="window-chrome-btn window-chrome-close" onClick={() => window.mirefir.closeWindow?.()}>
            <svg viewBox="0 0 12 12" className="h-3 w-3" fill="none" stroke="currentColor" strokeWidth="1.4" aria-hidden="true">
              <path d="M2 2l8 8M10 2L2 10" />
            </svg>
          </button>
        </div>
      </div>
    </>
  )
}
