let modelPromise = null

async function loadModel(url) {
  const mod = await import('vosk-browser')
  const Model = mod.Model || mod.default?.Model
  if (!Model) throw new Error('Голосовой модуль не загрузился')
  const model = new Model(url, -1)
  await new Promise((resolve, reject) => {
    let settled = false
    const done = (ok, error) => {
      if (settled) return
      settled = true
      if (ok) resolve(model)
      else reject(error || new Error('Не удалось загрузить голосовую модель'))
    }
    model.on('load', (message) => {
      done(Boolean(message?.result))
    })
    model.on('error', (message) => {
      done(false, new Error(message?.error || 'Ошибка голосовой модели'))
    })
  })
  return model
}

export function getVoskModel(url) {
  if (!modelPromise) {
    modelPromise = loadModel(url).catch((err) => {
      modelPromise = null
      throw err
    })
  }
  return modelPromise
}

export async function recognizePcm16(model, pcm, sampleRate = 16000) {
  if (!model || !pcm?.length) return ''
  const rec = new model.KaldiRecognizer(sampleRate)
  const float = new Float32Array(pcm.length)
  for (let i = 0; i < pcm.length; i += 1) float[i] = pcm[i] / 0x8000
  return new Promise((resolve) => {
    let best = ''
    rec.on('result', (message) => {
      const next = String(message?.result?.text || '').trim()
      if (next) best = next
    })
    rec.on('partialresult', (message) => {
      const next = String(message?.result?.partial || '').trim()
      if (next) best = next
    })
    rec.acceptWaveformFloat(float, sampleRate)
    rec.retrieveFinalResult()
    window.setTimeout(() => {
      try {
        rec.remove()
      } catch {
        /* ignore */
      }
      resolve(best)
    }, 700)
  })
}
