import { useCallback, useEffect, useRef, useState } from 'react'
import { getVoskModel, recognizePcm16 } from '../lib/voskClient.js'

function SpeechEngine() {
  return window.SpeechRecognition || window.webkitSpeechRecognition || null
}

function hasElectronVosk() {
  return typeof window.mirefir?.ensureVosk === 'function'
}

function errorText(code) {
  if (code === 'no-speech' || code === 'NO_RU') return 'Не услышали. Скажите название канала или «переключи на …».'
  if (code === 'audio-capture' || code === 'NO_MIC' || code === 'NO_AUDIO') return 'Микрофон недоступен. Проверьте устройство в Windows.'
  if (code === 'not-allowed' || code === 'service-not-allowed') {
    return 'Нужно разрешить доступ к микрофону в системе и в приложении.'
  }
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

export function useSpeechSearch(onResult, lang = 'ru-RU') {
  const recRef = useRef(null)
  const streamRef = useRef(null)
  const heardRef = useRef('')
  const genRef = useRef(0)
  const onResultRef = useRef(onResult)
  onResultRef.current = onResult
  const [listening, setListening] = useState(false)
  const [status, setStatus] = useState('')
  const [error, setError] = useState('')
  const supported = hasElectronVosk() || Boolean(SpeechEngine()) || Boolean(navigator.mediaDevices?.getUserMedia)

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
    setStatus('')
  }, [releaseMic])

  const start = useCallback(async () => {
    stop()
    setError('')
    setStatus('')
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

    if (hasElectronVosk()) {
      setListening(true)
      setStatus('Готовлю офлайн-распознавание…')
      const offProgress = window.mirefir.onVoskProgress?.((info) => {
        if (!stillThis()) return
        if (info?.text) setStatus(info.text)
      })
      try {
        const ready = await window.mirefir.ensureVosk()
        if (!stillThis()) return
        if (!ready?.ok) {
          setError(ready?.error || 'Не удалось подготовить голосовую модель.')
          return
        }
        setStatus('Слушаю…')
        const pcm = await recordPcm16(6.5, () => !stillThis())
        if (!stillThis()) return
        setStatus('Распознаю…')
        let model
        try {
          model = await getVoskModel(ready.url || 'mirefir-vosk://model.tar.gz')
        } catch {
          if (!ready.fileUrl) throw new Error('Голосовая модель недоступна')
          model = await getVoskModel(ready.fileUrl)
        }
        if (!stillThis()) return
        const text = await recognizePcm16(model, pcm)
        if (!stillThis()) return
        finishText(text, { grammar: 'dictation' })
      } catch (err) {
        if (stillThis()) setError(err.message || 'Не удалось распознать голос.')
      } finally {
        offProgress?.()
        if (stillThis()) {
          setListening(false)
          setStatus('')
        }
      }
      return
    }

    const Ctor = SpeechEngine()
    if (!Ctor) {
      setError('Голосовой поиск на этой платформе недоступен.')
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
  }, [lang, releaseMic, stop])

  useEffect(() => () => stop(), [stop])

  return { supported, listening, status, error, setError, start, stop }
}
