import { useEffect, useState } from 'react'
import { APP_VERSION } from '../lib/appInfo.js'
import { checkForUpdate, skipUpdate } from '../lib/updates.js'

export function UpdateDialog() {
  const [update, setUpdate] = useState(null)
  const [busy, setBusy] = useState(false)
  const [progress, setProgress] = useState('')
  const [error, setError] = useState('')

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
      if (!total) {
        setProgress(`Скачано ${Math.round(received / 1024 / 1024)} МБ`)
        return
      }
      setProgress(`Скачивание ${Math.min(100, Math.round((received / total) * 100))}%`)
    })
  }, [])

  if (!update) return null

  const install = async () => {
    setError('')
    if (window.mirefir?.downloadUpdate && window.mirefir?.applyUpdate) {
      setBusy(true)
      setProgress('Скачивание обновления…')
      try {
        await window.mirefir.downloadUpdate()
        setProgress('Установка… приложение закроется на пару секунд')
        await window.mirefir.applyUpdate()
      } catch (err) {
        setBusy(false)
        setError(err.message || 'Не удалось установить обновление')
      }
      return
    }
    const href = update.downloadUrl || update.pageUrl
    if (href) window.open(href, '_blank')
  }

  return (
    <div className="absolute inset-0 z-[60] flex items-center justify-center bg-black/70 p-4">
      <div className="w-full max-w-md rounded-2xl border border-white/10 bg-[#10151e] p-5">
        <div className="mb-1 text-lg font-semibold">Доступна новая версия</div>
        <p className="mb-4 text-sm text-white/50">
          Сейчас {APP_VERSION}, вышла {update.version}. Обновление скачается в фоне и установится само, без браузера.
          Плейлист, телепрограмма и настройки останутся.
        </p>
        {update.notes ? <p className="mb-4 max-h-28 overflow-y-auto text-xs text-white/40">{update.notes}</p> : null}
        {progress ? <p className="mb-3 text-sm text-sky-300">{progress}</p> : null}
        {error ? <p className="mb-3 text-sm text-red-300">{error}</p> : null}
        <div className="flex gap-2">
          <button type="button" className="rounded-xl bg-accent px-4 py-2 text-sm disabled:opacity-50" disabled={busy} onClick={install}>
            {busy ? 'Обновление…' : 'Обновить'}
          </button>
          <button
            type="button"
            className="rounded-xl bg-white/10 px-4 py-2 text-sm disabled:opacity-50"
            disabled={busy}
            onClick={() => {
              skipUpdate(update.version)
              setUpdate(null)
            }}
          >
            Пропустить
          </button>
        </div>
      </div>
    </div>
  )
}
