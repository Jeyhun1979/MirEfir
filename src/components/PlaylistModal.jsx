import { useEffect, useRef, useState } from 'react'
import { usePlayer } from '../store/PlayerContext.jsx'

export function PlaylistModal() {
  const fileRef = useRef(null)
  const {
    isModalOpen,
    setIsModalOpen,
    importFromUrl,
    importFromFile,
    importFromText,
    importEpg,
    setError,
    error,
    playlistUrl,
    epgUrl,
    needsSetup,
  } = usePlayer()
  const [url, setUrl] = useState(playlistUrl || '')
  const [guide, setGuide] = useState(epgUrl || '')
  const [busy, setBusy] = useState(false)

  const open = isModalOpen || needsSetup

  useEffect(() => {
    if (!open) return
    setUrl(playlistUrl || '')
    setGuide(epgUrl || '')
  }, [epgUrl, open, playlistUrl])

  if (!open) return null

  const close = () => {
    if (needsSetup) return
    setIsModalOpen(false)
    setError('')
  }

  const run = async (fn) => {
    setBusy(true)
    setError('')
    try {
      await fn()
      setIsModalOpen(false)
    } catch (err) {
      setError(err.message || 'Не удалось загрузить данные')
    } finally {
      setBusy(false)
    }
  }

  const loadGuideLater = (value) => {
    const next = value.trim()
    if (!next) return
    importEpg(next).catch((err) => setError(err.message || 'Не удалось загрузить телепрограмму'))
  }

  const openNative = async () => {
    if (window.mirefir?.openPlaylistFile) {
      const result = await window.mirefir.openPlaylistFile()
      if (result?.content) {
        await run(async () => {
          await importFromText(result.content, result.name)
          loadGuideLater(guide)
        })
      }
      return
    }
    fileRef.current?.click()
  }

  return (
    <div
      className={`absolute inset-0 z-50 flex items-center justify-center p-4 ${needsSetup ? 'bg-black' : 'bg-black/70'}`}
      onClick={close}
    >
      <div
        className="w-full max-w-lg rounded-2xl border border-white/10 bg-[#10151e] p-5 shadow-2xl"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="mb-2 text-lg font-semibold">{needsSetup ? 'Добро пожаловать в MirEfir' : 'Плейлист и телепрограмма'}</div>
        <p className="mb-4 text-sm text-white/50">
          Нужен плейлист: ссылка или файл M3U. Телепрограмму (XMLTV) можно указать сейчас или добавить позже в настройках.
        </p>

        <label className="mb-2 block text-xs uppercase tracking-wider text-white/40">Плейлист M3U *</label>
        <input
          autoFocus
          value={url}
          onChange={(event) => setUrl(event.target.value)}
          placeholder="https://example.com/playlist.m3u8"
          className="mb-3 w-full rounded-xl border border-white/10 bg-black/40 px-3 py-2.5 text-sm outline-none focus:border-accent"
        />

        <label className="mb-2 block text-xs uppercase tracking-wider text-white/40">Телепрограмма XMLTV (необязательно)</label>
        <input
          value={guide}
          onChange={(event) => setGuide(event.target.value)}
          placeholder="https://example.com/guide.xml.gz"
          className="mb-4 w-full rounded-xl border border-white/10 bg-black/40 px-3 py-2.5 text-sm outline-none focus:border-accent"
        />

        <div className="flex flex-wrap gap-2">
          <button
            type="button"
            disabled={busy || !url.trim()}
            onClick={() =>
              run(async () => {
                await importFromUrl(url.trim())
                loadGuideLater(guide)
              })
            }
            className="rounded-xl bg-accent px-4 py-2 text-sm font-medium disabled:opacity-40"
          >
            Открыть плейлист
          </button>
          <button
            type="button"
            disabled={busy}
            onClick={openNative}
            className="rounded-xl border border-white/10 bg-white/5 px-4 py-2 text-sm"
          >
            Файл M3U
          </button>
          {needsSetup ? null : (
            <button type="button" onClick={close} className="ml-auto text-sm text-white/50">
              Отмена
            </button>
          )}
        </div>

        <input
          ref={fileRef}
          type="file"
          accept=".m3u,.m3u8,audio/x-mpegurl,text/plain"
          className="hidden"
          onChange={(event) => {
            const file = event.target.files?.[0]
            if (!file) return
            run(async () => {
              await importFromFile(file)
              loadGuideLater(guide)
            })
          }}
        />

        {error ? <div className="mt-4 text-sm text-live">{error}</div> : null}
        {busy ? <div className="mt-3 text-sm text-white/50">Загрузка… не закрывайте окно</div> : null}
      </div>
    </div>
  )
}
