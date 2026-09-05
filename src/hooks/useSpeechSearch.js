import { useCallback, useEffect, useRef, useState } from 'react'

function SpeechEngine() {
  return window.SpeechRecognition || window.webkitSpeechRecognition || null
}

export function useSpeechSearch(onText, lang = 'ru-RU') {
  const recRef = useRef(null)
  const [listening, setListening] = useState(false)
  const [error, setError] = useState('')
  const supported = Boolean(SpeechEngine())

  const stop = useCallback(() => {
    recRef.current?.stop()
    recRef.current = null
    setListening(false)
  }, [])

  const start = useCallback(() => {
    const Ctor = SpeechEngine()
    if (!Ctor) {
      setError('Голос недоступен в этом браузере. Нужен Chrome, Edge или Electron.')
      return
    }

    stop()
    setError('')
    const rec = new Ctor()
    rec.lang = lang
    rec.interimResults = true
    rec.maxAlternatives = 1
    rec.continuous = false

    rec.onresult = (event) => {
      let text = ''
      for (let i = 0; i < event.results.length; i += 1) {
        text += event.results[i][0].transcript
      }
      onText(text.trim())
    }
    rec.onerror = (event) => {
      if (event.error !== 'aborted' && event.error !== 'no-speech') {
        setError('Не расслышали. Попробуйте ещё раз.')
      }
      setListening(false)
    }
    rec.onend = () => setListening(false)

    recRef.current = rec
    const begin = () => {
      rec.start()
      setListening(true)
    }
    if (navigator.mediaDevices?.getUserMedia) {
      navigator.mediaDevices
        .getUserMedia({ audio: true })
        .then((stream) => {
          stream.getTracks().forEach((track) => track.stop())
          begin()
        })
        .catch(() => {
          setError('Нужен доступ к микрофону (кнопка голоса на пульте или разрешение Android).')
        })
      return
    }
    begin()
  }, [lang, onText, stop])

  useEffect(() => () => stop(), [stop])

  return { supported, listening, error, start, stop }
}
