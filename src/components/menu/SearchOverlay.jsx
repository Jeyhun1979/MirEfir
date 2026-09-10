import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import { useSpeechSearch } from '../../hooks/useSpeechSearch.js'
import { parseVoiceCommand, pickChannelByVoice, voicePhrasesForChannels } from '../../lib/channelMatch.js'
import { arrowDir, isBackKey, isOkKey } from '../../lib/remoteKeys.js'
import { usePlayer } from '../../store/PlayerContext.jsx'

export function SearchOverlay() {
  const {
    uiScreen,
    searchQuery,
    setSearchQuery,
    closeOverlays,
    goBack,
    channels,
    visibleChannels,
    selectChannel,
    setListMode,
    voiceArmed,
    setVoiceArmed,
    settings,
    updateSettings,
  } = usePlayer()
  const inputRef = useRef(null)
  const listRef = useRef(null)
  const skipCursorReset = useRef(false)
  const [cursor, setCursor] = useState(-1)
  const [rowFocus, setRowFocus] = useState('input')
  const [voicePickId, setVoicePickId] = useState('')
  const [voiceNote, setVoiceNote] = useState('')
  const [permChoice, setPermChoice] = useState(0)
  const results = visibleChannels.slice(0, 40)
  const phrases = useMemo(() => voicePhrasesForChannels(channels), [channels])
  const phrasesRef = useRef(phrases)
  phrasesRef.current = phrases

  const onVoiceText = useCallback(
    (text, meta) => {
      if (!meta?.final) {
        setListMode('live')
        setSearchQuery(text)
        return
      }
      const parsed = parseVoiceCommand(text, meta.grammar || meta.intent || '')
      const query = parsed.query || text
      const hit = pickChannelByVoice(channels, query)
      setListMode('live')
      skipCursorReset.current = true
      setVoiceNote('')
      if (parsed.intent === 'switch') {
        if (hit) {
          selectChannel(hit.id)
          closeOverlays()
          return
        }
        setSearchQuery(query)
        setVoicePickId('')
        setCursor(-1)
        setVoiceNote('Канал не найден. Скажите «переключи на» и название из списка.')
        return
      }
      setSearchQuery(hit?.displayName || query)
      setVoicePickId(hit?.id || '')
      if (!hit) setCursor(-1)
    },
    [channels, closeOverlays, selectChannel, setListMode, setSearchQuery],
  )

  const speech = useSpeechSearch(onVoiceText, settings.language === 'en' ? 'en-US' : 'ru-RU', phrasesRef)
  const stopSpeech = speech.stop

  useEffect(() => {
    if (uiScreen === 'search') {
      setCursor(-1)
      setRowFocus(voiceArmed ? 'mic' : 'input')
      setVoicePickId('')
      setVoiceNote('')
      if (voiceArmed) inputRef.current?.blur()
      else inputRef.current?.focus()
      return
    }
    stopSpeech()
  }, [stopSpeech, uiScreen, voiceArmed])

  useEffect(() => {
    if (skipCursorReset.current) {
      skipCursorReset.current = false
      return
    }
    setCursor(-1)
    setVoicePickId('')
  }, [searchQuery])

  useEffect(() => {
    if (!voicePickId) return
    const index = results.findIndex((channel) => channel.id === voicePickId)
    if (index >= 0) setCursor(index)
  }, [results, voicePickId])

  const startVoice = speech.start
  useEffect(() => {
    if (uiScreen !== 'search' || !window.mirefir?.ensureVosk) return undefined
    window.mirefir.ensureVosk().catch(() => {})
    return undefined
  }, [uiScreen])

  const beginVoice = () => {
    if (!settings.voiceEnabled) {
      setVoiceNote('Микрофон выключен в меню. Включите пункт «Микрофон».')
      return
    }
    if (speech.listening) speech.stop()
    else startVoice()
  }

  useEffect(() => {
    if (uiScreen !== 'search' || !voiceArmed) return
    setVoiceArmed(false)
    if (!settings.voiceEnabled) {
      setVoiceNote('Микрофон выключен в меню. Включите пункт «Микрофон».')
      return
    }
    startVoice()
  }, [setVoiceArmed, settings.voiceEnabled, startVoice, uiScreen, voiceArmed])

  useEffect(() => {
    if (cursor < 0) return
    const node = listRef.current?.querySelector(`[data-hit="${cursor}"]`)
    node?.focus()
    node?.scrollIntoView({ block: 'nearest' })
  }, [cursor])

  useEffect(() => {
    if (uiScreen !== 'search') return undefined
    const onKey = (event) => {
      if (!isBackKey(event)) return
      event.preventDefault()
      event.stopPropagation()
      stopSpeech()
      goBack()
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [goBack, stopSpeech, uiScreen])

  if (uiScreen !== 'search') return null

  const pick = (channel) => {
    if (!channel) return
    selectChannel(channel.id)
    closeOverlays()
  }

  const onKeyDown = (event) => {
    const dir = arrowDir(event)
    if (speech.needPermission) {
      if (dir === 'left' || dir === 'right') {
        event.preventDefault()
        event.stopPropagation()
        setPermChoice((current) => (current + (dir === 'right' ? 1 : -1) + 3) % 3)
        return
      }
      if (isOkKey(event)) {
        event.preventDefault()
        event.stopPropagation()
        if (permChoice === 0) startVoice()
        else if (permChoice === 1) window.mirefir?.openMicSettings?.()
        else {
          updateSettings({ voiceEnabled: false })
          speech.setNeedPermission(false)
        }
        return
      }
    }
    if (isBackKey(event)) {
      event.preventDefault()
      event.stopPropagation()
      speech.stop()
      goBack()
      return
    }
    if ((dir === 'right' || dir === 'left') && cursor < 0) {
      event.preventDefault()
      event.stopPropagation()
      setRowFocus(dir === 'right' ? 'mic' : 'input')
      if (dir === 'right') inputRef.current?.blur()
      else inputRef.current?.focus()
      return
    }
    if (dir === 'down') {
      event.preventDefault()
      event.stopPropagation()
      if (!results.length) return
      setCursor((current) => Math.min(results.length - 1, current < 0 ? 0 : current + 1))
      inputRef.current?.blur()
      return
    }
    if (dir === 'up') {
      event.preventDefault()
      event.stopPropagation()
      if (cursor <= 0) {
        setCursor(-1)
        if (rowFocus === 'mic') inputRef.current?.blur()
        else inputRef.current?.focus()
        return
      }
      setCursor((current) => current - 1)
      return
    }
    if (isOkKey(event) && cursor < 0 && rowFocus === 'mic') {
      event.preventDefault()
      event.stopPropagation()
      beginVoice()
      return
    }
    if (isOkKey(event) && cursor >= 0) {
      event.preventDefault()
      event.stopPropagation()
      pick(results[cursor])
    }
  }

  return (
    <div className="absolute inset-0 z-50 flex items-start justify-center bg-black/75 p-8" onClick={closeOverlays}>
      <div
        className="w-full max-w-2xl rounded-2xl border border-white/10 bg-[#10151e] p-5"
        onClick={(event) => event.stopPropagation()}
        onKeyDown={onKeyDown}
      >
        <div className="mb-3 text-lg font-semibold">Поиск канала</div>
        <div className="mb-4 flex gap-2">
          <input
            ref={inputRef}
            value={searchQuery}
            onChange={(event) => {
              setListMode('live')
              setSearchQuery(event.target.value)
            }}
            onKeyDown={onKeyDown}
            placeholder="Название — найти. «Переключи на …» — включить"
            className={`min-w-0 flex-1 rounded-xl border bg-black/40 px-3 py-2.5 text-sm outline-none ${
              cursor < 0 && rowFocus === 'input' ? 'border-accent' : 'border-white/10'
            }`}
          />
          <button
            type="button"
            title={!settings.voiceEnabled ? 'Микрофон выключен в меню' : speech.supported ? 'Голосовой поиск' : 'Голос недоступен'}
            disabled={!speech.supported || !settings.voiceEnabled}
            onClick={() => {
              setRowFocus('mic')
              beginVoice()
            }}
            className={`remote-hit flex h-12 w-12 items-center justify-center rounded-xl ${
              speech.listening ? 'bg-live' : cursor < 0 && rowFocus === 'mic' ? 'bg-accent' : 'bg-white/10 hover:bg-white/15'
            } disabled:opacity-30`}
          >
            <svg viewBox="0 0 24 24" className="h-5 w-5" fill="currentColor" aria-hidden="true">
              <path d="M12 14a3 3 0 0 0 3-3V6a3 3 0 1 0-6 0v5a3 3 0 0 0 3 3zm5-3a5 5 0 0 1-10 0H5a7 7 0 0 0 6 6.9V21h2v-3.1A7 7 0 0 0 19 11h-2z" />
            </svg>
          </button>
        </div>
        {speech.listening || speech.status ? (
          <div className="mb-3 text-xs text-accent">
            {speech.status || 'Слушаю… название найдёт, «переключи на» сразу включит'}
          </div>
        ) : null}
        {speech.error ? <div className="mb-3 text-xs text-red-300">{speech.error}</div> : null}
        {voiceNote ? <div className="mb-3 text-xs text-red-300">{voiceNote}</div> : null}
        {speech.needPermission ? (
          <div className="mb-4 rounded-2xl border border-white/15 bg-black/50 p-4">
            <div className="text-sm font-medium">Нужен доступ к микрофону</div>
            <p className="mt-1 text-xs text-white/50">
              Разрешите микрофон для MirEfir в Windows. Подходят наушники, камера и встроенный микрофон — при смене устройства доступ запросится снова.
            </p>
            <div className="mt-3 flex flex-wrap gap-2">
              {[
                { id: 'retry', label: 'Разрешить', run: () => startVoice() },
                { id: 'win', label: 'Параметры Windows', run: () => window.mirefir?.openMicSettings?.() },
                {
                  id: 'off',
                  label: 'Выключить микрофон',
                  run: () => {
                    updateSettings({ voiceEnabled: false })
                    speech.setNeedPermission(false)
                  },
                },
              ].map((action, index) => (
                <button
                  key={action.id}
                  type="button"
                  onClick={action.run}
                  className={`remote-hit rounded-xl px-3 py-2 text-sm ${
                    permChoice === index ? 'bg-accent text-white ring-2 ring-white' : 'bg-white/10'
                  }`}
                >
                  {action.label}
                </button>
              ))}
            </div>
          </div>
        ) : null}
        <div ref={listRef} className="scroll-thin max-h-80 overflow-y-auto">
          {results.map((channel, index) => (
            <button
              key={channel.id}
              type="button"
              data-hit={index}
              onClick={() => pick(channel)}
              className={`remote-hit flex w-full items-center justify-between rounded-lg px-3 text-left ${
                cursor === index ? 'bg-accent/80' : 'hover:bg-white/5'
              }`}
            >
              <span>{channel.displayName}</span>
              <span className="text-xs text-white/35">{channel.group}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  )
}
