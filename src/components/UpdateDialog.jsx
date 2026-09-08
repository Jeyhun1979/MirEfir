import { useEffect, useState } from 'react'
import { APP_VERSION } from '../lib/appInfo.js'
import { checkForUpdate, skipUpdate } from '../lib/updates.js'
import { usePlayer } from '../store/PlayerContext.jsx'

function progressLabel(received, total) {
  if (!total) return `Скачано ${Math.round(received / 1024 / 1024)} МБ`
  return `${Math.min(100, Math.round((received / total) * 100))}%`
}

export function UpdateDialog() {
  const { isFullscreen } = usePlayer()
  const [update, setUpdate] = useState(null)
  const [phase, setPhase] = useState('offer')
  const [progress, setProgress] = useState('')
  const [error, setError] = useState('')
  const [hidden, setHidden] = useState(false)

  useEffect(() => {
    const start = async () => {
      await new Promise((resolve) => window.setTimeout(resolve, 4000))
      try {
        const info = await window.mirefir?.appInfo?.()
        if (info?.packaged && window.mirefir?.checkUpdate) {
          setUpdate(await window.mirefir.checkUpdate())
          return
        }
        let kind = ''
        if (info?.portable) kind = 'portable'
        else if (/Android/i.test(navigator.userAgent)) kind = 'android'
        setUpdate(await checkForUpdate(kind))
      } catch {
        setUpdate(null)
      }
    }
    start()
  }, [])

  useEffect(() => {
    if (!window.mirefir?.onUpdateProgress) return undefined
    return window.mirefir.onUpdateProgress(({ received, total }) => {
      setProgress(progressLabel(received, total))
    })
  }, [])

  useEffect(() => {
    if (phase === 'ready' || phase === 'error') setHidden(false)
  }, [phase])

  useEffect(() => {
    if (!update || !window.mirefir?.downloadUpdate) return undefined
    let cancelled = false
    setPhase('downloading')
    setHidden(true)
    setProgress('Скачивание…')
    window.mirefir
      .downloadUpdate()
      .then(() => {
        if (cancelled) return
        setPhase('ready')
        setHidden(false)
      })
      .catch((err) => {
        if (cancelled) return
        setPhase('error')
        setError(err.message || 'Не удалось скачать обновление')
        setHidden(false)
      })
    return () => {
      cancelled = true
    }
  }, [update])

  if (!update || hidden) return null

  const canApply = Boolean(window.mirefir?.downloadUpdate && window.mirefir?.applyUpdate)

  const downloadInBackground = async () => {
    if (!canApply) {
      const href = update.downloadUrl || update.pageUrl
      if (href) window.open(href, '_blank')
      return
    }
    setError('')
    setPhase('downloading')
    setProgress('Скачивание…')
    try {
      await window.mirefir.downloadUpdate()
      setPhase('ready')
      setProgress('')
    } catch (err) {
      setPhase('error')
      setError(err.message || 'Не удалось скачать обновление')
    }
  }

  const installNow = async () => {
    setError('')
    setPhase('installing')
    try {
      if (phase !== 'ready') await window.mirefir.downloadUpdate()
      await window.mirefir.applyUpdate()
    } catch (err) {
      setPhase('error')
      setError(err.message || 'Не удалось установить обновление')
    }
  }

  const later = () => setHidden(true)
  const skipForever = () => {
    skipUpdate(update.version)
    setUpdate(null)
  }

  return (
    <div className="pointer-events-none absolute inset-0 z-[60]">
      <div className={`pointer-events-auto absolute right-4 w-[min(22rem,calc(100%-2rem))] rounded-2xl border border-white/15 bg-[#10151e]/95 p-3 shadow-2xl backdrop-blur-md ${isFullscreen ? 'bottom-6' : 'bottom-20'}`}>
        {phase === 'offer' ? (
          <>
            <div className="text-sm font-semibold">Доступна {update.version}</div>
            <p className="mt-1 text-xs text-white/50">
              Сейчас {APP_VERSION}. Скачается в фоне, эфир не перекрывается. Плейлист и настройки останутся.
            </p>
            <div className="mt-3 flex flex-wrap gap-2">
              <button type="button" className="remote-hit rounded-xl bg-accent px-3 py-1.5 text-sm" onClick={downloadInBackground}>
                Скачать в фоне
              </button>
              <button type="button" className="rounded-xl bg-white/10 px-3 py-1.5 text-sm" onClick={later}>
                Позже
              </button>
              <button type="button" className="rounded-xl px-2 py-1.5 text-xs text-white/40" onClick={skipForever}>
                Пропустить
              </button>
            </div>
          </>
        ) : null}

        {phase === 'downloading' ? (
          <>
            <div className="text-sm font-semibold">Скачивается {update.version}</div>
            <p className="mt-1 text-xs text-sky-300">{progress || 'Скачивание…'}</p>
            <p className="mt-1 text-xs text-white/45">Можно смотреть дальше. Установка начнётся, когда нажмёте кнопку.</p>
            <button type="button" className="mt-2 rounded-xl bg-white/10 px-3 py-1.5 text-sm" onClick={later}>
              Скрыть
            </button>
          </>
        ) : null}

        {phase === 'ready' ? (
          <>
            <div className="text-sm font-semibold">Обновление готово</div>
            <p className="mt-1 text-xs text-white/50">Эфир прервётся на установку, потом приложение откроется само.</p>
            <div className="mt-3 flex gap-2">
              <button type="button" className="remote-hit rounded-xl bg-accent px-3 py-1.5 text-sm" onClick={installNow}>
                Установить
              </button>
              <button type="button" className="rounded-xl bg-white/10 px-3 py-1.5 text-sm" onClick={later}>
                Позже
              </button>
            </div>
          </>
        ) : null}

        {phase === 'installing' ? (
          <>
            <div className="text-sm font-semibold">Установка…</div>
            <p className="mt-1 text-xs text-sky-300">Приложение закроется. Если не откроется само — запустите ярлык.</p>
          </>
        ) : null}

        {phase === 'error' ? (
          <>
            <div className="text-sm font-semibold">Не удалось обновить</div>
            <p className="mt-1 text-xs text-red-300">{error}</p>
            <div className="mt-3 flex gap-2">
              <button type="button" className="remote-hit rounded-xl bg-accent px-3 py-1.5 text-sm" onClick={downloadInBackground}>
                Повторить
              </button>
              <button type="button" className="rounded-xl bg-white/10 px-3 py-1.5 text-sm" onClick={later}>
                Скрыть
              </button>
            </div>
          </>
        ) : null}
      </div>
    </div>
  )
}
