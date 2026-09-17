import { Capacitor, registerPlugin } from '@capacitor/core'

const WhisperNative = registerPlugin('Whisper')

function toBase64(buffer) {
  const bytes = buffer instanceof ArrayBuffer ? new Uint8Array(buffer) : new Uint8Array(buffer.buffer || buffer)
  let binary = ''
  const chunk = 0x8000
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk))
  }
  return btoa(binary)
}

export function hasWhisper() {
  return Boolean(window.mirefir?.transcribeSpeech) || Capacitor.isNativePlatform()
}

export async function ensureWhisper(onProgress) {
  if (window.mirefir?.ensureSpeech) {
    const off = window.mirefir.onSpeechProgress?.(onProgress)
    try {
      return await window.mirefir.ensureSpeech()
    } finally {
      off?.()
    }
  }
  if (!Capacitor.isNativePlatform()) return { ok: false, error: 'NO_WHISPER' }
  const listen = await WhisperNative.addListener?.('progress', (info) => onProgress?.(info))
  try {
    return await WhisperNative.ensure()
  } finally {
    await listen?.remove?.()
  }
}

export async function transcribeWhisper(pcm, sampleRate, onProgress) {
  if (window.mirefir?.transcribeSpeech) {
    const off = window.mirefir.onSpeechProgress?.(onProgress)
    try {
      return await window.mirefir.transcribeSpeech({ pcm, sampleRate })
    } finally {
      off?.()
    }
  }
  if (!Capacitor.isNativePlatform()) return { ok: false, error: 'NO_WHISPER' }
  const listen = await WhisperNative.addListener?.('progress', (info) => onProgress?.(info))
  try {
    return await WhisperNative.transcribe({ pcm: toBase64(pcm), sampleRate })
  } finally {
    await listen?.remove?.()
  }
}

export function cancelWhisper() {
  window.mirefir?.cancelSpeech?.()
  if (Capacitor.isNativePlatform()) WhisperNative.cancel?.()
}
