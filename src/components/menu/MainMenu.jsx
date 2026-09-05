import { useEffect, useRef, useState } from 'react'
import { App as CapApp } from '@capacitor/app'
import { Capacitor } from '@capacitor/core'
import { arrowDir, isOkKey } from '../../lib/remoteKeys.js'
import { usePlayer } from '../../store/PlayerContext.jsx'

const ITEMS = [
  { id: 'live', title: 'Телевидение', hint: 'Эфир и категории' },
  { id: 'movies', title: 'Фильмы', hint: 'Кино из плейлиста' },
  { id: 'series', title: 'Сериалы', hint: 'Сериальные группы' },
  { id: 'guide', title: 'Телепрограмма', hint: 'Сетка EPG' },
  { id: 'recordings', title: 'Записи', hint: 'DVR и расписание' },
  { id: 'multiview', title: 'Мультиэкран', hint: 'Несколько каналов сразу' },
  { id: 'search', title: 'Поиск', hint: 'Найти канал или голос' },
  { id: 'playlists', title: 'Плейлисты', hint: 'M3U и Xtream' },
  { id: 'favorites', title: 'Избранное', hint: 'Каналы со звездой' },
  { id: 'history', title: 'История', hint: 'Недавно смотрели' },
  { id: 'archive', title: 'Архив', hint: 'Catch-up' },
  { id: 'settings', title: 'Настройки', hint: 'Все параметры плеера' },
  { id: 'exit', title: 'Выход', hint: 'Закрыть приложение' },
]

export function MainMenu() {
  const { uiScreen, closeOverlays, openSettings, setListMode, setUiScreen, setSelectedGroupId, setFocusZone } =
    usePlayer()
  const [cursor, setCursor] = useState(0)
  const itemRefs = useRef([])

  useEffect(() => {
    if (uiScreen === 'menu') setCursor(0)
  }, [uiScreen])

  useEffect(() => {
    itemRefs.current[cursor]?.scrollIntoView({ block: 'nearest' })
  }, [cursor])

  useEffect(() => {
    if (uiScreen !== 'menu') return undefined
    const onKey = (event) => {
      const dir = arrowDir(event)
      if (dir === 'up' || dir === 'down') {
        event.preventDefault()
        event.stopPropagation()
        setCursor((current) => (current + (dir === 'down' ? 1 : -1) + ITEMS.length) % ITEMS.length)
        return
      }
      if (isOkKey(event)) {
        event.preventDefault()
        event.stopPropagation()
        run(ITEMS[cursor].id)
      }
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [cursor, uiScreen])

  if (uiScreen !== 'menu') return null

  const run = (id) => {
    if (id === 'live') {
      setListMode('live')
      setSelectedGroupId('all')
      closeOverlays()
      return
    }
    if (id === 'movies') {
      setListMode('movies')
      closeOverlays()
      return
    }
    if (id === 'series') {
      setListMode('series')
      closeOverlays()
      return
    }
    if (id === 'guide') {
      setListMode('live')
      closeOverlays()
      setFocusZone('channels')
      return
    }
    if (id === 'recordings') {
      setUiScreen('recordings')
      return
    }
    if (id === 'multiview') {
      setUiScreen('multiview')
      return
    }
    if (id === 'playlists') {
      openSettings('playlists')
      return
    }
    if (id === 'favorites') {
      setListMode('live')
      setSelectedGroupId('favorites')
      closeOverlays()
      return
    }
    if (id === 'history') {
      setListMode('live')
      setSelectedGroupId('recent')
      closeOverlays()
      return
    }
    if (id === 'archive') {
      setListMode('archive')
      closeOverlays()
      return
    }
    if (id === 'search') {
      setUiScreen('search')
      return
    }
    if (id === 'settings') {
      openSettings('playlists')
      return
    }
    if (id === 'exit') {
      if (Capacitor.isNativePlatform()) CapApp.exitApp().catch(() => closeOverlays())
      else window.close()
      closeOverlays()
    }
  }

  return (
    <div className="absolute inset-0 z-50 flex bg-black/70" onClick={closeOverlays}>
      <aside
        className="flex h-full w-[min(400px,86vw)] flex-col border-r border-white/10 bg-[#0c111a] shadow-2xl"
        onClick={(event) => event.stopPropagation()}
      >
        <div className="border-b border-white/8 px-6 py-5">
          <div className="text-lg font-semibold">Меню</div>
          <div className="text-xs text-white/40">Пульт: ↑↓ и OK · гиромышь: навести и клик · Назад — закрыть</div>
        </div>
        <div className="scroll-thin flex-1 overflow-y-auto py-2">
          {ITEMS.map((item, index) => (
            <button
              key={item.id}
              ref={(node) => {
                itemRefs.current[index] = node
              }}
              type="button"
              onMouseEnter={() => setCursor(index)}
              onClick={() => run(item.id)}
              className={`remote-hit flex w-full items-center justify-between px-6 text-left ${
                index === cursor ? 'bg-accent text-white' : 'hover:bg-white/8'
              }`}
            >
              <span>
                <span className="block text-[16px]">{item.title}</span>
                <span className="block text-[12px] text-white/40">{item.hint}</span>
              </span>
              <span className="text-white/25">›</span>
            </button>
          ))}
        </div>
      </aside>
    </div>
  )
}
