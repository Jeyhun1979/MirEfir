import { useCallback, useEffect, useRef, useState } from 'react'

function SpeechEngine() {
  return window.SpeechRecognition || window.webkitSpeechRecognition || null
}

function hasElectronSpeech() {
  return typeof window.mirefir?.listenSpeech === 'function' && /windows/i.test(navigator.userAgent)
}

function errorText(code) {
  if (code === 'no-speech' || code === 'NO_RU') return 'Не услышали. Скажите название канала или «переключи на …».'
  if (code === 'audio-capture' || code === 'NO_MIC' || code === 'NO_AUDIO') return 'Микрофон недоступен. Проверьте устройство в Windows.'
  if (code === 'not-allowed' || code === 'service-not-allowed') {
    return 'Нужно разрешить доступ к микрофону в системе и в приложении.'
  }
  if (code === 'NO_LANG') return 'Распознавание речи Windows недоступно. Введите название текстом.'
  if (code === 'NO_PS') return 'Не удалось запустить распознавание речи Windows.'
  if (code === 'network') return 'Не удалось распознать голос. Проверьте интернет и повторите.'
  if (code === 'aborted') return ''
  return 'Не услышали. Скажите название канала или «переключи на …».'
}

function mergeFloat(chunks) {
  let total = 0
  for (const chunk of chunks) total += chunk.length
  const out = new Float32Array(total)
  let offset = 0
  for (const chunk of chunks) {
    out.set(chunk, offset)
    offset += chunk.length
  }
  return out
}

function toPcm16(float32, sampleRate) {
  const ratio = (Number(sampleRate) || 48000) / 16000
  const length = Math.max(1, Math.floor(float32.length / ratio))
  const out = new Int16Array(length)
  for (let i = 0; i < length; i += 1) {
    const sample = Math.max(-1, Math.min(1, float32[Math.floor(i * ratio)] || 0))
    out[i] = sample < 0 ? sample * 0x8000 : sample * 0x7fff
  }
  return out
}

async function recordPcm16(seconds, isCancelled) {
  const stream = await navigator.mediaDevices.getUserMedia({
    audio: { channelCount: 1, echoCancellation: true, noiseSuppression: true },
  })
  const ctx = new AudioContext()
  const source = ctx.createMediaStreamSource(stream)
  const processor = ctx.createScriptProcessor(4096, 1, 1)
  const chunks = []
  processor.onaudioprocess = (event) => {
    chunks.push(new Float32Array(event.inputBuffer.getChannelData(0)))
  }
  const mute = ctx.createGain()
  mute.gain.value = 0
  source.connect(processor)
  processor.connect(mute)
  mute.connect(ctx.destination)
  const started = Date.now()
  while (Date.now() - started < seconds * 1000) {
    if (isCancelled()) break
    await new Promise((resolve) => window.setTimeout(resolve, 120))
  }
  const rate = ctx.sampleRate
  processor.disconnect()
  source.disconnect()
  mute.disconnect()
  stream.getTracks().forEach((track) => track.stop())
  await ctx.close().catch(() => {})
  return toPcm16(mergeFloat(chunks), rate)
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
  const supported = hasElectronSpeech() || Boolean(SpeechEngine()) || Boolean(navigator.mediaDevices?.getUserMedia)

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
    const stillThis = () => gen === genRef.current

    const finishText = (text, extra = {}) => {
      const next = String(text || '').trim()
      if (!next) {
        setError('Не услышали. Скажите название канала или «переключи на …».')
        return
      }
      heardRef.current = next
      emit(next, { final: true, ...extra })
    }

    const listenOnline = async () => {
      if (!window.mirefir?.transcribeSpeech || !navigator.mediaDevices?.getUserMedia) return null
      const pcm = await recordPcm16(6.5, () => !stillThis())
      if (!stillThis()) return { cancelled: true }
      const bytes = new Uint8Array(pcm.buffer, pcm.byteOffset, pcm.byteLength)
      return window.mirefir.transcribeSpeech({ pcm: bytes, lang })
    }

    if (hasElectronSpeech()) {
      setListening(true)
      try {
        const result = await window.mirefir.listenSpeech({
          phrases: phrasesRef?.current || [],
          seconds: 8,
        })
        if (!stillThis()) return
        if (result?.ok && String(result.text || '').trim()) {
          finishText(result.text, {
            intent: result.intent || '',
            grammar: result.grammar || '',
            confidence: result.confidence || 0,
          })
          return
        }
        if (result?.error && result.error !== 'NO_RU' && result.error !== 'NO_LANG' && result.error !== 'network') {
          setError(errorText(result.error))
          return
        }
        const online = await listenOnline()
        if (!stillThis() || online?.cancelled) return
        if (!online?.ok) {
          setError(errorText(online?.error || result?.error || 'no-speech'))
          return
        }
        finishText(online.text, { grammar: online.grammar || 'dictation' })
      } catch {
        if (stillThis()) setError('Не удалось запустить распознавание речи.')
      } finally {
        if (stillThis()) setListening(false)
      }
      return
    }

    const Ctor = SpeechEngine()
    if (!Ctor) {
      setListening(true)
      try {
        const online = await listenOnline()
        if (!stillThis() || online?.cancelled) return
        if (!online?.ok) {
          setError(errorText(online?.error || 'no-speech'))
          return
        }
        finishText(online.text, { grammar: online.grammar || 'dictation' })
      } catch {
        if (stillThis()) setError('Нужен доступ к микрофону.')
      } finally {
        if (stillThis()) setListening(false)
      }
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

    if (!stillThis()) return

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
