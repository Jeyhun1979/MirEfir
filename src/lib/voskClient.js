let cached = { url: '', promise: null }

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
  if (cached.promise && cached.url === url) return cached.promise
  cached.url = url
  cached.promise = loadModel(url).catch((err) => {
    cached.promise = null
    cached.url = ''
    throw err
  })
  return cached.promise
}

export function createLiveRecognizer(model, sampleRate = 16000) {
  const rec = new model.KaldiRecognizer(sampleRate)
  let best = ''
  rec.on('result', (message) => {
    const next = String(message?.result?.text || '').trim()
    if (next) best = next
  })
  rec.on('partialresult', (message) => {
    const next = String(message?.result?.partial || '').trim()
    if (next) best = next
  })
  return {
    push(float32) {
      if (float32?.length) rec.acceptWaveformFloat(float32, sampleRate)
    },
    text() {
      return best
    },
    finish() {
      return new Promise((resolve) => {
        rec.retrieveFinalResult()
        const started = Date.now()
        const tick = () => {
          if (best || Date.now() - started > 3500) {
            try {
              rec.remove()
            } catch {
              /* ignore */
            }
            resolve(best)
            return
          }
          window.setTimeout(tick, 120)
        }
        window.setTimeout(tick, 250)
      })
    },
  }
}
