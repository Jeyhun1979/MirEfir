import { useCallback, useEffect, useRef } from 'react'
import { useSpeechSearch } from '../../hooks/useSpeechSearch.js'
import { usePlayer } from '../../store/PlayerContext.jsx'

export function SearchOverlay() {
  const {
    uiScreen,
    searchQuery,
    setSearchQuery,
    closeOverlays,
    visibleChannels,
    selectChannel,
    setListMode,
    voiceArmed,
    setVoiceArmed,
    settings,
  } = usePlayer()
  const inputRef = useRef(null)

  const onVoiceText = useCallback(
    (text) => {
      setListMode('live')
      setSearchQuery(text)
    },
    [setListMode, setSearchQuery],
  )

  const speech = useSpeechSearch(onVoiceText, settings.language === 'en' ? 'en-US' : 'ru-RU')

  useEffect(() => {
    if (uiScreen === 'search') inputRef.current?.focus()
  }, [uiScreen])

  const startVoice = speech.start
  useEffect(() => {
    if (uiScreen !== 'search' || !voiceArmed) return
    setVoiceArmed(false)
    startVoice()
  }, [setVoiceArmed, startVoice, uiScreen, voiceArmed])

  if (uiScreen !== 'search') return null

  return (
    <div className="absolute inset-0 z-50 flex items-start justify-center bg-black/75 p-8" onClick={closeOverlays}>
      <div
        className="w-full max-w-2xl rounded-2xl border border-white/10 bg-[#10151e] p-5"
        onClick={(event) => event.stopPropagation()}
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
            placeholder="Название канала или нажмите микрофон"
            className="min-w-0 flex-1 rounded-xl border border-white/10 bg-black/40 px-3 py-2.5 text-sm outline-none focus:border-accent"
          />
          <button
            type="button"
            title={speech.supported ? 'Голосовой поиск (V)' : 'Голос недоступен'}
            disabled={!speech.supported}
            onClick={() => (speech.listening ? speech.stop() : speech.start())}
            className={`remote-hit flex h-12 w-12 items-center justify-center rounded-xl ${
              speech.listening ? 'bg-live' : 'bg-white/10 hover:bg-white/15'
            } disabled:opacity-30`}
          >
            <svg viewBox="0 0 24 24" className="h-5 w-5" fill="currentColor" aria-hidden="true">
              <path d="M12 14a3 3 0 0 0 3-3V6a3 3 0 1 0-6 0v5a3 3 0 0 0 3 3zm5-3a5 5 0 0 1-10 0H5a7 7 0 0 0 6 6.9V21h2v-3.1A7 7 0 0 0 19 11h-2z" />
            </svg>
          </button>
        </div>
        {speech.listening ? <div className="mb-3 text-xs text-accent">Слушаю… говорите название канала</div> : null}
        {speech.error ? <div className="mb-3 text-xs text-red-300">{speech.error}</div> : null}
        <div className="scroll-thin max-h-80 overflow-y-auto">
          {visibleChannels.slice(0, 40).map((channel) => (
            <button
              key={channel.id}
              type="button"
              onClick={() => {
                selectChannel(channel.id)
                closeOverlays()
              }}
              className="remote-hit flex w-full items-center justify-between rounded-lg px-3 text-left hover:bg-white/5"
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
