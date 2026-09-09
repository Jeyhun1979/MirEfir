import { useEffect, useMemo, useRef, useState } from 'react'
import { StreamTile } from '../player/StreamTile.jsx'
import { formatBytes, listStoredRecordings, recordingPlayUrl } from '../../lib/storage.js'
import { arrowDir, isBackKey, isOkKey } from '../../lib/remoteKeys.js'
import { usePlayer } from '../../store/PlayerContext.jsx'

function readLocalRecordings() {
  try {
    return JSON.parse(localStorage.getItem('mirefir.recordings') || '[]')
  } catch {
    return []
  }
}

function layoutClass(count) {
  if (count <= 1) return 'grid-cols-1 grid-rows-1'
  if (count === 2) return 'grid-cols-2 grid-rows-1'
  return 'grid-cols-2 grid-rows-2'
}

export function RecordingsScreen() {
  const { uiScreen, closeOverlays, goBack, openSettings, settings } = usePlayer()
  const [items, setItems] = useState(readLocalRecordings)
  const [playing, setPlaying] = useState(null)
  const [playError, setPlayError] = useState('')

  useEffect(() => {
    if (uiScreen !== 'recordings') return undefined
    let cancelled = false
    setItems(readLocalRecordings())
    listStoredRecordings(settings.recordingPath)
      .then((disk) => {
        if (cancelled || !disk?.length) return
        setItems((current) => {
          const known = new Set(current.map((item) => item.file))
          const extra = disk
            .filter((item) => !known.has(item.path) && !known.has(item.name))
            .map((item) => ({
              id: item.path,
              title: item.name,
              file: item.path,
              bytes: item.bytes,
              ended: item.mtime,
            }))
          return extra.length ? [...extra, ...current] : current
        })
      })
      .catch(() => {})
    return () => {
      cancelled = true
    }
  }, [settings.recordingPath, uiScreen])

  useEffect(() => {
    if (uiScreen !== 'recordings') return undefined
    const onKey = (event) => {
      if (!isBackKey(event)) return
      event.preventDefault()
      event.stopPropagation()
      if (playing) {
        setPlaying(null)
        return
      }
      goBack()
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [goBack, playing, uiScreen])

  if (uiScreen !== 'recordings') return null

  return (
    <div className="absolute inset-0 z-50 flex items-center justify-center bg-black/75 p-6" onClick={closeOverlays}>
      <div className="flex max-h-[86vh] w-full max-w-2xl flex-col rounded-2xl border border-white/10 bg-[#10151e] p-6" onClick={(e) => e.stopPropagation()}>
        <div className="mb-1 text-xl font-semibold">Записи</div>
        <p className="mb-4 text-sm text-white/45">
          Куда писать — спросим: внутренняя память или диск/флешка.
          {settings.recordingPath ? ` Сейчас: ${settings.recordingPath}` : ' Папка ещё не выбрана.'}
        </p>

        {playing ? (
          <div className="mb-4 overflow-hidden rounded-xl bg-black">
            <video src={playing.url} className="max-h-64 w-full" controls autoPlay />
            {playError ? <div className="px-3 py-2 text-xs text-red-300">{playError}</div> : null}
            <button type="button" className="w-full bg-white/5 py-2 text-xs text-white/50" onClick={() => setPlaying(null)}>
              Закрыть просмотр
            </button>
          </div>
        ) : null}

        <div className="scroll-thin min-h-0 flex-1 space-y-2 overflow-y-auto">
          {items.length === 0 ? (
            <div className="rounded-xl border border-dashed border-white/10 px-4 py-10 text-center text-sm text-white/35">
              Нет записанных передач. Откройте канал и нажмите REC или R.
            </div>
          ) : (
            items.map((item) => (
              <button
                key={item.id || item.file}
                type="button"
                className="flex w-full items-center justify-between rounded-xl bg-white/5 px-4 py-3 text-left hover:bg-white/8"
                onClick={async () => {
                  setPlayError('')
                  try {
                    const url = await recordingPlayUrl(item.file)
                    setPlaying({ url, title: item.title })
                  } catch (err) {
                    setPlayError(err.message || 'Не удалось открыть файл')
                  }
                }}
              >
                <div className="min-w-0">
                  <div className="truncate text-sm">{item.title}</div>
                  <div className="text-xs text-white/35">
                    {item.ended ? new Date(item.ended).toLocaleString('ru-RU') : ''}
                    {item.bytes ? ` · ${formatBytes(item.bytes)}` : ''}
                  </div>
                </div>
                <span className="text-xs text-accent">Смотреть</span>
              </button>
            ))
          )}
        </div>

        <div className="mt-5 flex gap-2">
          <button type="button" className="rounded-lg bg-accent px-4 py-2 text-sm" onClick={() => openSettings('recordings')}>
            Выбрать диск
          </button>
          <button type="button" className="rounded-lg bg-white/10 px-4 py-2 text-sm" onClick={closeOverlays}>
            Закрыть
          </button>
        </div>
      </div>
    </div>
  )
}

export function MultiViewScreen() {
  const { uiScreen, closeOverlays, goBack, channels, selectedChannel } = usePlayer()
  const [tiles, setTiles] = useState([])
  const [focused, setFocused] = useState(0)
  const [picker, setPicker] = useState(false)
  const [query, setQuery] = useState('')
  const seedRef = useRef(selectedChannel)
  seedRef.current = selectedChannel

  useEffect(() => {
    if (uiScreen !== 'multiview') {
      setTiles([])
      setFocused(0)
      setPicker(false)
      setQuery('')
      return
    }
    setTiles(seedRef.current ? [seedRef.current] : [])
    setFocused(0)
    setPicker(false)
    setQuery('')
  }, [uiScreen])

  useEffect(() => {
    if (uiScreen !== 'multiview') return undefined
    const onKey = (event) => {
      if (!isBackKey(event)) return
      event.preventDefault()
      event.stopPropagation()
      if (picker) {
        setPicker(false)
        return
      }
      goBack()
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [goBack, picker, uiScreen])

  const matches = useMemo(() => {
    const q = query.trim().toLowerCase()
    const used = new Set(tiles.map((item) => item.id))
    return channels
      .filter((channel) => !used.has(channel.id))
      .filter((channel) => !q || channel.displayName.toLowerCase().includes(q) || String(channel.number).includes(q))
      .slice(0, 80)
  }, [channels, query, tiles])

  if (uiScreen !== 'multiview') return null

  const addChannel = (channel) => {
    setTiles((current) => (current.length >= 4 ? current : [...current, channel]))
    setFocused(tiles.length)
    setPicker(false)
    setQuery('')
  }

  return (
    <div className="absolute inset-0 z-50 flex flex-col bg-black p-4">
      <div className="mb-3 flex items-center justify-between gap-3">
        <div>
          <div className="text-lg font-semibold">Мультиэкран</div>
          <div className="text-xs text-white/40">Окна добавляются по одному. Звук только в выбранном. Максимум 4.</div>
        </div>
        <div className="flex items-center gap-2">
          {tiles.length < 4 ? (
            <button type="button" className="rounded-lg bg-accent px-3 py-2 text-sm" onClick={() => setPicker(true)}>
              + Добавить окно
            </button>
          ) : null}
          <button type="button" className="rounded-lg bg-white/10 px-3 py-2 text-sm" onClick={closeOverlays}>
            Закрыть
          </button>
        </div>
      </div>

      {tiles.length === 0 ? (
        <button
          type="button"
          className="flex min-h-0 flex-1 items-center justify-center rounded-2xl border border-dashed border-white/15 text-white/50"
          onClick={() => setPicker(true)}
        >
          Добавить первый канал
        </button>
      ) : (
        <div className={`grid min-h-0 flex-1 gap-2 ${layoutClass(tiles.length)}`}>
          {tiles.map((channel, index) => (
            <div key={`${channel.id}-${index}`} className={`min-h-0 h-full ${tiles.length === 3 && index === 2 ? 'col-span-2' : ''}`}>
              <StreamTile
                channel={channel}
                compact
                muted={focused !== index}
                onFocus={() => setFocused(index)}
                onRemove={
                  tiles.length > 1
                    ? () => {
                        setTiles((current) => current.filter((_, itemIndex) => itemIndex !== index))
                        setFocused(0)
                      }
                    : undefined
                }
              />
            </div>
          ))}
        </div>
      )}

      {picker ? (
        <div className="absolute inset-0 z-10 flex items-end justify-center bg-black/65 p-6" onClick={() => setPicker(false)}>
          <div className="w-full max-w-xl rounded-2xl border border-white/10 bg-[#10151e] p-4" onClick={(e) => e.stopPropagation()}>
            <div className="mb-3 text-sm font-medium">Куда добавить окно?</div>
            <input
              autoFocus
              className="mb-3 w-full rounded-lg border border-white/10 bg-black/40 px-3 py-2 text-sm outline-none"
              placeholder="Название или номер канала"
              value={query}
              onChange={(e) => setQuery(e.target.value)}
            />
            <div className="scroll-thin max-h-72 space-y-1 overflow-y-auto">
              {matches.map((channel) => (
                <button
                  key={channel.id}
                  type="button"
                  className="flex w-full items-center justify-between rounded-lg px-3 py-2 text-left text-sm hover:bg-white/8"
                  onClick={() => addChannel(channel)}
                >
                  <span className="truncate">{channel.displayName}</span>
                  <span className="text-xs text-white/30">{channel.number}</span>
                </button>
              ))}
              {matches.length === 0 ? <div className="px-3 py-6 text-center text-sm text-white/35">Ничего не найдено</div> : null}
            </div>
          </div>
        </div>
      ) : null}
    </div>
  )
}

export function HistoryScreen() {
  const { uiScreen, closeOverlays, goBack, openMenu, watchHistory, clearHistory, selectChannel, channels } = usePlayer()
  const [cursor, setCursor] = useState(0)

  const items = watchHistory || []

  useEffect(() => {
    if (uiScreen === 'history') setCursor(0)
  }, [uiScreen])

  useEffect(() => {
    if (uiScreen !== 'history') return undefined
    const onKey = (event) => {
      const dir = arrowDir(event)
      if (dir === 'up' || dir === 'down') {
        event.preventDefault()
        event.stopPropagation()
        if (!items.length) return
        setCursor((current) => (current + (dir === 'down' ? 1 : -1) + items.length) % items.length)
        return
      }
      if (isBackKey(event)) {
        event.preventDefault()
        event.stopPropagation()
        goBack()
        return
      }
      if (isOkKey(event) && items[cursor]) {
        event.preventDefault()
        event.stopPropagation()
        selectChannel(items[cursor].id)
        closeOverlays()
      }
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [closeOverlays, cursor, goBack, items, selectChannel, uiScreen])

  if (uiScreen !== 'history') return null

  const openItem = (item) => {
    const exists = channels.find((channel) => channel.id === item.id)
    if (!exists) return
    selectChannel(item.id)
    closeOverlays()
  }

  return (
    <div className="absolute inset-0 z-50 flex items-center justify-center bg-black/75 p-6" onClick={closeOverlays}>
      <div
        className="flex max-h-[86vh] w-full max-w-lg flex-col rounded-2xl border border-white/10 bg-[#10151e] p-6"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="mb-1 text-xl font-semibold">История</div>
        <p className="mb-4 text-sm text-white/45">Последние 10 каналов. Назад — в меню.</p>
        <div className="scroll-thin min-h-0 flex-1 space-y-1 overflow-y-auto">
          {items.length === 0 ? (
            <div className="rounded-xl border border-dashed border-white/10 px-4 py-10 text-center text-sm text-white/35">
              История пуста. Переключите канал — он появится здесь.
            </div>
          ) : (
            items.map((item, index) => (
              <button
                key={`${item.id}-${item.at}`}
                type="button"
                onMouseEnter={() => setCursor(index)}
                onClick={() => openItem(item)}
                className={`remote-hit flex w-full items-center justify-between rounded-xl px-4 py-3 text-left ${
                  index === cursor ? 'bg-accent text-white' : 'bg-white/5 hover:bg-white/8'
                }`}
              >
                <div className="min-w-0">
                  <div className="truncate text-sm">{item.name || 'Канал'}</div>
                  <div className={`truncate text-xs ${index === cursor ? 'text-white/70' : 'text-white/40'}`}>
                    {item.program || 'Прямой эфир'}
                    {item.at ? ` · ${new Date(item.at).toLocaleString('ru-RU', { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' })}` : ''}
                  </div>
                </div>
              </button>
            ))
          )}
        </div>
        <div className="mt-5 flex gap-2">
          <button type="button" className="rounded-lg bg-white/10 px-4 py-2 text-sm" onClick={clearHistory} disabled={!items.length}>
            Очистить историю
          </button>
          <button type="button" className="rounded-lg bg-accent px-4 py-2 text-sm" onClick={openMenu}>
            Меню
          </button>
          <button type="button" className="rounded-lg bg-white/10 px-4 py-2 text-sm" onClick={closeOverlays}>
            Закрыть
          </button>
        </div>
      </div>
    </div>
  )
}
