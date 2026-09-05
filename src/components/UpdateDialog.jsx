import { useEffect, useState } from 'react'
import { APP_VERSION } from '../lib/appInfo.js'
import { checkForUpdate, skipUpdate } from '../lib/updates.js'

export function UpdateDialog() {
  const [update, setUpdate] = useState(null)

  useEffect(() => {
    checkForUpdate().then(setUpdate)
  }, [])

  if (!update) return null

  return (
    <div className="absolute inset-0 z-[60] flex items-center justify-center bg-black/70 p-4">
      <div className="w-full max-w-md rounded-2xl border border-white/10 bg-[#10151e] p-5">
        <div className="mb-1 text-lg font-semibold">Доступна новая версия</div>
        <p className="mb-4 text-sm text-white/50">
          Сейчас {APP_VERSION}, вышла {update.version}. Можно скачать обновление или пропустить.
        </p>
        {update.notes ? <p className="mb-4 max-h-28 overflow-y-auto text-xs text-white/40">{update.notes}</p> : null}
        <div className="flex gap-2">
          <button
            type="button"
            className="rounded-xl bg-accent px-4 py-2 text-sm"
            onClick={() => {
              const href = update.downloadUrl || update.pageUrl
              if (window.oneplayer?.openExternal) window.oneplayer.openExternal(href)
              else window.open(href, '_blank')
            }}
          >
            Обновить
          </button>
          <button
            type="button"
            className="rounded-xl bg-white/10 px-4 py-2 text-sm"
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
