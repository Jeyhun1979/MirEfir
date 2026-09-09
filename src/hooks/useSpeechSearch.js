import { useCallback, useEffect, useRef, useState } from 'react'

function SpeechEngine() {
  return window.SpeechRecognition || window.webkitSpeechRecognition || null
}

function hasElectronSpeech() {
  return typeof window.mirefir?.listenSpeech === 'function' && /windows/i.test(navigator.userAgent)
}

function errorText(code) {
  if (code === 'no-speech') return 'Не услышали. Скажите название канала или «переключи на …».'
  if (code === 'audio-capture' || code === 'NO_MIC') return 'Микрофон недоступен. Проверьте устройство в Windows.'
  if (code === 'not-allowed' || code === 'service-not-allowed') {
    return 'Нужно разрешить доступ к микрофону в системе и в приложении.'
  }
  if (code === 'NO_LANG') return 'Распознавание речи Windows недоступно. Введите название текстом.'
  if (code === 'NO_PS') return 'Не удалось запустить распознавание речи Windows.'
  if (code === 'network') return 'Голос через браузер недоступен. Введите название текстом.'
  if (code === 'aborted') return ''
  return 'Не услышали. Скажите название канала или «переключи на …».'
}

export function useSpeechSearch(onResult, lang = 'ru-RU', phrasesRef) {
  const recRef = useRef(null)
  const streamRef = useRef(null)
  const heardRef = useRef('')
  const genRef = useRef(0)
  const onResultRef = useRef(onResult)
  onResultRef.current = onResult
  const [listening, setListening] = useState(false)
  const [error, setError] = useState('')
  const supported = hasElectronSpeech() || Boolean(SpeechEngine())

  const releaseMic = useCallback(() => {
    streamRef.current?.getTracks().forEach((track) => track.stop())
    streamRef.current = null
  }, [])

  const stop = useCallback(() => {
    genRef.current += 1
    try {
      recRef.current?.stop()
    } catch {
      /* already stopped */
    }
    recRef.current = null
    window.mirefir?.cancelSpeech?.()
    releaseMic()
    setListening(false)
  }, [releaseMic])

  const start = useCallback(async () => {
    stop()
    setError('')
    heardRef.current = ''
    const gen = genRef.current
    const emit = (text, extra) => onResultRef.current(text, extra)

    if (hasElectronSpeech()) {
      setListening(true)
      try {
        const result = await window.mirefir.listenSpeech({
          phrases: phrasesRef?.current || [],
          seconds: 8,
        })
        if (gen !== genRef.current) return
        if (!result?.ok) {
          setError(errorText(result?.error))
          return
        }
        const text = String(result.text || '').trim()
        if (!text) {
          setError('Не услышали. Скажите название канала или «переключи на …».')
          return
        }
        heardRef.current = text
        emit(text, {
          final: true,
          intent: result.intent || '',
          grammar: result.grammar || '',
          confidence: result.confidence || 0,
        })
      } catch {
        if (gen === genRef.current) setError('Не удалось запустить распознавание речи Windows.')
      } finally {
        if (gen === genRef.current) setListening(false)
      }
      return
    }

    const Ctor = SpeechEngine()
    if (!Ctor) {
      setError('Голос недоступен в этом плеере. Поиск можно ввести текстом.')
      return
    }

    if (navigator.mediaDevices?.getUserMedia) {
      try {
        const stream = await navigator.mediaDevices.getUserMedia({ audio: true })
        stream.getTracks().forEach((track) => track.stop())
      } catch {
        setError('Нужен доступ к микрофону.')
        return
      }
    }

    if (gen !== genRef.current) return

    const rec = new Ctor()
    rec.lang = lang
    rec.interimResults = true
    rec.maxAlternatives = 3
    rec.continuous = true

    rec.onstart = () => setListening(true)
    rec.onresult = (event) => {
      let text = ''
      let isFinal = false
      for (let i = 0; i < event.results.length; i += 1) {
        text += event.results[i][0].transcript
        if (event.results[i].isFinal) isFinal = true
      }
      const next = text.trim()
      if (!next) return
      heardRef.current = next
      emit(next, { final: isFinal })
      if (isFinal) stop()
    }
    rec.onerror = (event) => {
      const message = errorText(event.error)
      if (message) setError(message)
      if (event.error !== 'no-speech') stop()
    }
    rec.onend = () => {
      if (recRef.current === rec && !heardRef.current) {
        setError('Не услышали. Скажите название канала или «переключи на …».')
      }
      if (recRef.current === rec) recRef.current = null
      releaseMic()
      setListening(false)
    }

    recRef.current = rec
    try {
      rec.start()
    } catch (err) {
      setError(err.message || 'Не удалось запустить распознавание.')
      releaseMic()
    }

    window.setTimeout(() => {
      if (recRef.current === rec) stop()
    }, 8000)
  }, [lang, phrasesRef, releaseMic, stop])

  useEffect(() => () => stop(), [stop])

  return { supported, listening, error, setError, start, stop }
}
