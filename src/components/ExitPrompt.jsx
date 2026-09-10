import { useEffect, useState } from 'react'
import { arrowDir, isBackKey, isOkKey } from '../lib/remoteKeys.js'
import { quitApp } from '../lib/quitApp.js'
import { usePlayer } from '../store/PlayerContext.jsx'

const HOLD_MS = 4000

export function ExitPrompt() {
  const { exitPrompt, setExitPrompt } = usePlayer()
  const [choice, setChoice] = useState(0)

  useEffect(() => {
    if (!exitPrompt) return undefined
    setChoice(0)
    const timer = window.setTimeout(() => setExitPrompt(false), HOLD_MS)
    return () => window.clearTimeout(timer)
  }, [exitPrompt, setExitPrompt])

  useEffect(() => {
    if (!exitPrompt) return undefined

    const onKey = (event) => {
      const dir = arrowDir(event)
      if (!(dir || isOkKey(event) || isBackKey(event))) return
      event.preventDefault()
      event.stopImmediatePropagation()
      if (isBackKey(event)) {
        quitApp()
        return
      }
      if (dir === 'left' || dir === 'up') {
        setChoice(0)
        return
      }
      if (dir === 'right' || dir === 'down') {
        setChoice(1)
        return
      }
      if (isOkKey(event)) {
        if (choice === 0) quitApp()
        else setExitPrompt(false)
      }
    }

    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [choice, exitPrompt, setExitPrompt])

  if (!exitPrompt) return null

  const actions = [
    { id: 'exit', label: 'Выйти', run: () => quitApp() },
    { id: 'cancel', label: 'Отмена', run: () => setExitPrompt(false) },
  ]

  return (
    <div className="fixed inset-0 z-[90] flex items-center justify-center bg-black/45" onClick={() => setExitPrompt(false)}>
      <div
        className="w-[min(28rem,calc(100%-2rem))] rounded-2xl border border-white/15 bg-[#10151e] px-6 py-5 text-center shadow-2xl"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="text-[17px] font-medium">Для выхода из приложения нажмите ещё раз «Назад»!</div>
        <div className="mt-5 flex justify-center gap-3">
          {actions.map((action, index) => (
            <button
              key={action.id}
              type="button"
              onClick={action.run}
              className={`remote-hit min-h-11 rounded-2xl px-5 text-base ${
                index === choice ? 'bg-accent text-white ring-2 ring-white' : 'bg-white/10 text-white/80'
              }`}
            >
              {action.label}
            </button>
          ))}
        </div>
      </div>
    </div>
  )
}
