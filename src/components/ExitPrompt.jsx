import { useEffect } from 'react'
import { arrowDir, isBackKey, isOkKey } from '../lib/remoteKeys.js'
import { quitApp } from '../lib/quitApp.js'
import { usePlayer } from '../store/PlayerContext.jsx'

export function ExitPrompt() {
  const { exitPrompt, setExitPrompt } = usePlayer()

  useEffect(() => {
    if (!exitPrompt) return undefined

    const onKey = (event) => {
      const dir = arrowDir(event)
      if (!(dir || isOkKey(event) || isBackKey(event))) return
      event.preventDefault()
      event.stopPropagation()
      if (dir === 'left' || isOkKey(event)) {
        quitApp()
        return
      }
      setExitPrompt(false)
    }

    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [exitPrompt, setExitPrompt])

  if (!exitPrompt) return null

  return (
    <div className="fixed inset-0 z-[70] flex items-center justify-center bg-black/45" onClick={() => setExitPrompt(false)}>
      <div
        className="max-w-sm rounded-2xl border border-white/15 bg-[#10151e] px-6 py-5 text-center shadow-2xl"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="text-[17px] font-medium">Выход из приложения</div>
        <p className="mt-2 text-sm text-white/60">Для выхода нажмите ещё раз влево</p>
      </div>
    </div>
  )
}
