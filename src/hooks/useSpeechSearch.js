import { useCallback, useEffect, useRef, useState } from 'react'

function SpeechEngine() {
  return window.SpeechRecognition || window.webkitSpeechRecognition || null
}

function errorText(code) {
  if (code === 'no-speech') return 'Не услышали речь. Нажмите микрофон и скажите название канала.'
  if (code === 'audio-capture') return 'Микрофон недоступен. Проверьте устройство.'
  if (code === 'not-allowed' || code === 'service-not-allowed') {
    return 'Нужно разрешить доступ к микрофону в системе и в приложении.'
  }
  if (code === 'network') return 'Голосовой поиск требует интернет. Проверьте сеть и попробуйте ещё раз.'
  if (code === 'aborted') return ''
  return 'Не удалось распознать. Нажмите микрофон и говорите 2–3 секунды.'
}

export function useSpeechSearch(onText, lang = 'ru-RU') {
  const recRef = useRef(null)
  const streamRef = useRef(null)
  const heardRef = useRef('')
  const [listening, setListening] = useState(false)
  const [error, setError] = useState('')
  const supported = Boolean(SpeechEngine())

  const releaseMic = useCallback(() => {
    streamRef.current?.getTracks().forEach((track) => track.stop())
    streamRef.current = null
  }, [])

  const stop = useCallback(() => {
    try {
      recRef.current?.stop()
    } catch {
      /* already stopped */
    }
    recRef.current = null
    releaseMic()
    setListening(false)
  }, [releaseMic])

  const start = useCallback(async () => {
    const Ctor = SpeechEngine()
    if (!Ctor) {
      setError('Голос недоступен в этом плеере. Поиск можно ввести текстом.')
      return
    }

    stop()
    setError('')
    heardRef.current = ''

    if (navigator.mediaDevices?.getUserMedia) {
      try {
        const stream = await navigator.mediaDevices.getUserMedia({ audio: true })
        stream.getTracks().forEach((track) => track.stop())
      } catch {
        setError('Нужен доступ к микрофону (разрешение Windows или Android).')
        return
      }
    }

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
      onText(next)
      if (isFinal) stop()
    }
    rec.onerror = (event) => {
      const message = errorText(event.error)
      if (message) setError(message)
      if (event.error !== 'no-speech') stop()
    }
    rec.onend = () => {
      if (recRef.current === rec && !heardRef.current) {
        setError('Не услышали речь. Нажмите микрофон и скажите название канала.')
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
      return
    }

    window.setTimeout(() => {
      if (recRef.current === rec) stop()
    }, 8000)
  }, [lang, onText, releaseMic, stop])

  useEffect(() => () => stop(), [stop])

  return { supported, listening, error, start, stop }
}
