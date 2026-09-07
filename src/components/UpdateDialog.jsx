import { useEffect, useState } from 'react'
import { APP_VERSION } from '../lib/appInfo.js'
import { checkForUpdate, skipUpdate } from '../lib/updates.js'

export function UpdateDialog() {
  const [update, setUpdate] = useState(null)
  const [busy, setBusy] = useState(false)
  const [progress, setProgress] = useState('')
  const [error, setError] = useState('')

  useEffect(() => {
    let kind = ''
    const start = async () => {
      try {
        const info = await window.mirefir?.appInfo?.()
        if (info?.portable) kind = 'portable'
        else if (info?.packaged) kind = 'nsis'
        else if (/Android/i.test(navigator.userAgent)) kind = 'android'
      } catch {
        /* browser */
      }
      setUpdate(await checkForUpdate(kind))
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
    if (!window.mirefir?.downloadUpdate || !update.downloadUrl) {
      const href = update.downloadUrl || update.pageUrl
      if (window.mirefir?.openExternal) window.mirefir.openExternal(href)
      else window.open(href, '_blank')
      return
    }
    setBusy(true)
    setProgress('Скачивание обновления…')
    try {
      const filePath = await window.mirefir.downloadUpdate(update.downloadUrl)
      setProgress('Установка… приложение закроется на пару секунд')
      await window.mirefir.applyUpdate(filePath)
    } catch (err) {
      setBusy(false)
      setError(err.message || 'Не удалось установить обновление')
    }
  }

  return (
    <div className="absolute inset-0 z-[60] flex items-center justify-center bg-black/70 p-4">
      <div className="w-full max-w-md rounded-2xl border border-white/10 bg-[#10151e] p-5">
        <div className="mb-1 text-lg font-semibold">Доступна новая версия</div>
        <p className="mb-4 text-sm text-white/50">
          Сейчас {APP_VERSION}, вышла {update.version}. Плейлист, телепрограмма и настройки останутся. Обновление скачается и установится само.
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
