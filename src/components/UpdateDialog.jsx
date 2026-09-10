import { useEffect, useState } from 'react'
import { APP_VERSION } from '../lib/appInfo.js'
import { checkForUpdate, skipUpdate } from '../lib/updates.js'
import { arrowDir, isBackKey, isOkKey } from '../lib/remoteKeys.js'

function progressLabel(received, total) {
  if (!total) return `Скачано ${Math.round(received / 1024 / 1024)} МБ`
  return `${Math.min(100, Math.round((received / total) * 100))}%`
}

export function UpdateDialog() {
  const [update, setUpdate] = useState(null)
  const [phase, setPhase] = useState('offer')
  const [progress, setProgress] = useState('')
  const [error, setError] = useState('')
  const [hidden, setHidden] = useState(false)
  const [choice, setChoice] = useState(0)

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
    if (phase === 'ready' || phase === 'error') {
      setHidden(false)
      setChoice(0)
    }
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

  const actions =
    phase === 'ready'
      ? [
          { id: 'install', label: 'Установить', primary: true, run: installNow },
          { id: 'later', label: 'Позже', run: later },
        ]
      : phase === 'error'
        ? [
            { id: 'retry', label: 'Повторить', primary: true, run: downloadInBackground },
            { id: 'hide', label: 'Скрыть', run: later },
          ]
        : phase === 'offer'
          ? [
              { id: 'download', label: 'Скачать в фоне', primary: true, run: downloadInBackground },
              { id: 'later', label: 'Позже', run: later },
              { id: 'skip', label: 'Пропустить', run: skipForever },
            ]
          : []

  useEffect(() => {
    if (!update || hidden || !actions.length) return undefined
    const onKey = (event) => {
      const dir = arrowDir(event)
      if (!(dir || isOkKey(event) || isBackKey(event))) return
      event.preventDefault()
      event.stopImmediatePropagation()
      if (isBackKey(event)) {
        later()
        return
      }
      if (dir === 'left' || dir === 'up') {
        setChoice((current) => (current + actions.length - 1) % actions.length)
        return
      }
      if (dir === 'right' || dir === 'down') {
        setChoice((current) => (current + 1) % actions.length)
        return
      }
      if (isOkKey(event)) actions[choice]?.run?.()
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [actions, choice, hidden, update])

  if (!update || hidden) return null

  return (
    <div
      className="absolute inset-0 z-[80] flex items-center justify-center bg-black/55 p-6"
      onClick={later}
    >
      <div
        className="w-[min(32rem,calc(100%-2rem))] rounded-3xl border border-white/15 bg-[#10151e] px-7 py-6 shadow-2xl"
        onClick={(event) => event.stopPropagation()}
      >
        {phase === 'offer' ? (
          <>
            <div className="text-xl font-semibold">Доступна {update.version}</div>
            <p className="mt-2 text-sm text-white/55">
              Сейчас {APP_VERSION}. Скачается в фоне, эфир не перекрывается. Плейлист и настройки останутся.
            </p>
          </>
        ) : null}

        {phase === 'downloading' ? (
          <>
            <div className="text-xl font-semibold">Скачивается {update.version}</div>
            <p className="mt-2 text-sm text-sky-300">{progress || 'Скачивание…'}</p>
            <p className="mt-1 text-sm text-white/45">Можно смотреть дальше. Установка начнётся, когда нажмёте кнопку.</p>
          </>
        ) : null}

        {phase === 'ready' ? (
          <>
            <div className="text-xl font-semibold">Обновление готово</div>
            <p className="mt-2 text-sm text-white/55">
              Эфир прервётся на установку, потом приложение откроется само. Плейлист и настройки останутся.
            </p>
          </>
        ) : null}

        {phase === 'installing' ? (
          <>
            <div className="text-xl font-semibold">Установка</div>
            <p className="mt-2 text-sm text-sky-300">На экране останется окно «Установка», пока приложение не откроется снова.</p>
          </>
        ) : null}

        {phase === 'error' ? (
          <>
            <div className="text-xl font-semibold">Не удалось обновить</div>
            <p className="mt-2 text-sm text-red-300">{error}</p>
          </>
        ) : null}

        {actions.length ? (
          <div className="mt-5 flex flex-wrap gap-3">
            {actions.map((action, index) => (
              <button
                key={action.id}
                type="button"
                onClick={action.run}
                className={`remote-hit min-h-11 rounded-2xl px-5 text-base ${
                  index === choice
                    ? action.primary
                      ? 'bg-accent text-white ring-2 ring-white'
                      : 'bg-white/20 ring-2 ring-white'
                    : action.primary
                      ? 'bg-accent/80 text-white'
                      : 'bg-white/10 text-white/80'
                }`}
              >
                {action.label}
              </button>
            ))}
          </div>
        ) : null}
      </div>
    </div>
  )
}
