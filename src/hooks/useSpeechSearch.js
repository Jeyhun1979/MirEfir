import { useCallback, useEffect, useRef, useState } from 'react'
import { cancelWhisper, ensureWhisper, hasWhisper, transcribeWhisper } from '../lib/whisperBridge.js'

function isDenied(err) {
  const name = String(err?.name || '')
  const message = String(err?.message || '').toLowerCase()
  return name === 'NotAllowedError' || name === 'SecurityError' || message.includes('permission') || message.includes('denied')
}

function downsample(float32, fromRate, toRate = 16000) {
  if (!float32?.length) return float32
  if (Math.abs(fromRate - toRate) < 1) return float32
  const ratio = fromRate / toRate
  const length = Math.max(1, Math.floor(float32.length / ratio))
  const out = new Float32Array(length)
  for (let i = 0; i < length; i += 1) {
    const src = i * ratio
    const i0 = Math.floor(src)
    const i1 = Math.min(float32.length - 1, i0 + 1)
    const t = src - i0
    out[i] = float32[i0] * (1 - t) + float32[i1] * t
  }
  return out
}

function rms(float32) {
  if (!float32?.length) return 0
  let sum = 0
  for (let i = 0; i < float32.length; i += 1) sum += float32[i] * float32[i]
  return Math.sqrt(sum / float32.length)
}

function floatToInt16(float32) {
  const out = new Int16Array(float32.length)
  for (let i = 0; i < float32.length; i += 1) {
    const sample = Math.max(-1, Math.min(1, float32[i] || 0))
    out[i] = sample < 0 ? sample * 0x8000 : sample * 0x7fff
  }
  return out
}

function mergeFloat(parts) {
  const total = parts.reduce((sum, part) => sum + (part?.length || 0), 0)
  const out = new Float32Array(total)
  let offset = 0
  for (const part of parts) {
    if (!part?.length) continue
    out.set(part, offset)
    offset += part.length
  }
  return out
}

async function openMicrophone() {
  const attempts = [{ audio: { channelCount: 1, echoCancellation: true, noiseSuppression: true } }, { audio: true }]
  try {
    const devices = await navigator.mediaDevices.enumerateDevices()
    for (const device of devices) {
      if (device.kind === 'audioinput' && device.deviceId) {
        attempts.push({ audio: { deviceId: { exact: device.deviceId } } })
      }
    }
  } catch {
    /* keep default attempts */
  }
  let lastError
  for (const constraints of attempts) {
    try {
      return await navigator.mediaDevices.getUserMedia(constraints)
    } catch (err) {
      lastError = err
    }
  }
  throw lastError || new Error('NO_MIC')
}

async function captureUtterance(stream, stillThis, onLevel) {
  const ctx = new AudioContext()
  await ctx.resume()
  const source = ctx.createMediaStreamSource(stream)
  const processor = ctx.createScriptProcessor(4096, 1, 1)
  const mute = ctx.createGain()
  mute.gain.value = 0
  const SPEECH_RMS = 0.015
  const SILENCE_MS = 2000
  const MAX_LISTEN_MS = 10000
  const WAIT_FOR_SPEECH_MS = 5000
  const voice = { peak: 0, loudChunks: 0, lastLoudAt: 0 }
  const parts = []
  processor.onaudioprocess = (event) => {
    const input = event.inputBuffer.getChannelData(0)
    const level = rms(input)
    voice.peak = Math.max(voice.peak, level)
    if (level >= SPEECH_RMS) {
      voice.loudChunks += 1
      voice.lastLoudAt = Date.now()
    }
    onLevel?.(level)
    parts.push(downsample(new Float32Array(input), ctx.sampleRate, 16000))
  }
  source.connect(processor)
  processor.connect(mute)
  mute.connect(ctx.destination)
  const started = Date.now()
  while (stillThis()) {
    const now = Date.now()
    const speaking = voice.loudChunks >= 2
    if (speaking && voice.lastLoudAt && now - voice.lastLoudAt >= SILENCE_MS) break
    if (!speaking && now - started >= WAIT_FOR_SPEECH_MS) break
    if (now - started >= MAX_LISTEN_MS) break
    await new Promise((resolve) => window.setTimeout(resolve, 80))
  }
  processor.disconnect()
  source.disconnect()
  mute.disconnect()
  await ctx.close().catch(() => {})
  return { voice, pcm: mergeFloat(parts) }
}

function cleanHeard(text) {
  const next = String(text || '')
    .replace(/\[unk\]/gi, ' ')
    .replace(/\s+/g, ' ')
    .trim()
  if (/deprecated|main\.exe|whisper-cli|WARNING:/i.test(next)) return ''
  return next
}

export function useSpeechSearch(onResult) {
  const streamRef = useRef(null)
  const heardRef = useRef('')
  const genRef = useRef(0)
  const onResultRef = useRef(onResult)
  onResultRef.current = onResult
  const [listening, setListening] = useState(false)
  const [status, setStatus] = useState('')
  const [error, setError] = useState('')
  const [needPermission, setNeedPermission] = useState(false)
  const supported = hasWhisper() || Boolean(navigator.mediaDevices?.getUserMedia)

  const releaseMic = useCallback(() => {
    streamRef.current?.getTracks().forEach((track) => track.stop())
    streamRef.current = null
  }, [])

  const stop = useCallback(() => {
    genRef.current += 1
    cancelWhisper()
    releaseMic()
    setListening(false)
    setStatus('')
  }, [releaseMic])

  const start = useCallback(async () => {
    stop()
    setError('')
    setNeedPermission(false)
    setStatus('')
    heardRef.current = ''
    const gen = genRef.current
    const emit = (text, extra) => onResultRef.current(text, extra)
    const stillThis = () => gen === genRef.current
    const onProgress = (info) => {
      if (!stillThis()) return
      if (info?.text) setStatus(info.text)
    }
    setListening(true)

    const finishText = (text, extra = {}) => {
      const next = cleanHeard(text)
      if (!next) {
        setError('Не услышали. Скажите название канала или «переключи на …».')
        return
      }
      heardRef.current = next
      emit(next, { final: true, ...extra })
    }

    let localStream = null
    const dropStream = () => {
      localStream?.getTracks().forEach((track) => track.stop())
      if (streamRef.current === localStream) streamRef.current = null
      localStream = null
    }
    try {
      const readyPromise = ensureWhisper(onProgress)
      setStatus('Открываю микрофон…')
      try {
        localStream = await openMicrophone()
        if (!stillThis()) {
          dropStream()
          return
        }
        streamRef.current = localStream
      } catch (err) {
        if (!stillThis()) return
        if (isDenied(err)) {
          setNeedPermission(true)
          setError('Нужен доступ к микрофону.')
        } else {
          setError('Микрофон недоступен. Проверьте устройство.')
        }
        return
      }

      const ready = await readyPromise
      if (!stillThis()) return
      if (!ready?.ok) {
        setError(ready?.error || 'Не удалось подготовить голосовую модель.')
        return
      }

      setStatus('Слушаю…')
      const { voice, pcm } = await captureUtterance(localStream, stillThis)
      dropStream()
      if (!stillThis()) return
      if (voice.peak < 0.008) {
        setError('Микрофон молчит. Проверьте, какой микрофон выбран.')
        return
      }

      setStatus('Распознаю…')
      const samples = floatToInt16(pcm)
      const result = await transcribeWhisper(
        samples.buffer.slice(samples.byteOffset, samples.byteOffset + samples.byteLength),
        16000,
        onProgress,
      )
      if (!stillThis()) return
      if (!result?.ok && result?.error && result.error !== 'NO_AUDIO') {
        throw new Error(result.error)
      }
      finishText(String(result?.text || '').trim())
    } catch (err) {
      if (stillThis()) setError(err.message || 'Не удалось распознать голос.')
    } finally {
      dropStream()
      if (stillThis()) {
        setListening(false)
        setStatus('')
      }
    }
  }, [stop])

  useEffect(() => () => stop(), [stop])

  return { supported, listening, status, error, needPermission, setNeedPermission, setError, start, stop }
}
