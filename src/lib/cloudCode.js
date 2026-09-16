function toBase64(text) {
  const bytes = new TextEncoder().encode(text)
  let bin = ''
  bytes.forEach((byte) => {
    bin += String.fromCharCode(byte)
  })
  return btoa(bin)
}

function fromBase64(code) {
  const bin = atob(code)
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i += 1) bytes[i] = bin.charCodeAt(i)
  return new TextDecoder().decode(bytes)
}

function portableSettings(settings) {
  if (!settings || typeof settings !== 'object') return null
  const next = { ...settings }
  delete next.recordingPath
  return next
}

export function encodeCloudCode({ favoriteNames = [], playlists = [], epgUrl = '', settings = null }) {
  const payload = {
    v: 1,
    f: favoriteNames.filter(Boolean).slice(0, 200),
    p: playlists.map((item) => (typeof item === 'string' ? item : item.url)).filter(Boolean),
    e: epgUrl || '',
    s: portableSettings(settings),
  }
  return `ME1.${toBase64(JSON.stringify(payload))}`
}

export function copyText(text) {
  return new Promise((resolve, reject) => {
    const fallback = () => {
      const el = document.createElement('textarea')
      el.value = text
      el.setAttribute('readonly', '')
      el.style.position = 'fixed'
      el.style.left = '-9999px'
      document.body.appendChild(el)
      el.focus()
      el.select()
      el.setSelectionRange(0, text.length)
      let ok = false
      try {
        ok = document.execCommand('copy')
      } catch {
        ok = false
      }
      document.body.removeChild(el)
      if (ok) resolve('copied')
      else reject(new Error('Не удалось скопировать. Выделите код и скопируйте вручную.'))
    }

    if (navigator.clipboard?.writeText) {
      navigator.clipboard.writeText(text).then(() => resolve('copied')).catch(fallback)
      return
    }
    fallback()
  })
}

export async function shareCloudCode(code) {
  const text = `MirEfir — код настроек и избранного:\n${code}`
  if (navigator.share) {
    try {
      await navigator.share({ title: 'MirEfir', text })
      return 'shared'
    } catch (err) {
      if (err.name === 'AbortError') return 'cancel'
    }
  }
  await copyText(code)
  return 'copied'
}

export function decodeCloudCode(raw) {
  const code = String(raw || '').trim().replace(/\s+/g, '')
  if (!code) throw new Error('Вставьте код')
  const body = code.startsWith('ME1.') || code.startsWith('OP1.') ? code.slice(4) : code
  let data
  try {
    data = JSON.parse(fromBase64(body))
  } catch {
    throw new Error('Код повреждён или скопирован не полностью')
  }
  if (!data || (data.v !== 1 && data.v !== 2)) throw new Error('Неизвестный формат кода')
  const playlistUrls = Array.isArray(data.p)
    ? data.p.map((item) => (typeof item === 'string' ? item : item?.url)).filter(Boolean)
    : data.p
      ? [data.p]
      : []
  return {
    favoriteNames: Array.isArray(data.f) ? data.f : [],
    playlistUrls,
    epgUrl: data.e || '',
    settings: data.s && typeof data.s === 'object' ? data.s : null,
  }
}

export async function shareOrSaveJson(fileName, data) {
  const text = typeof data === 'string' ? data : JSON.stringify(data, null, 2)
  if (window.mirefir?.saveBackup) {
    const result = await window.mirefir.saveBackup({ fileName, text })
    if (result?.canceled) return 'cancel'
    if (result?.ok) return 'saved'
  }
  if (window.showSaveFilePicker) {
    try {
      const handle = await window.showSaveFilePicker({
        suggestedName: fileName,
        types: [{ description: 'JSON', accept: { 'application/json': ['.json'] } }],
      })
      const writable = await handle.createWritable()
      await writable.write(text)
      await writable.close()
      return 'saved'
    } catch (err) {
      if (err.name === 'AbortError') return 'cancel'
    }
  }
  const blob = new Blob([text], { type: 'application/json' })
  const file = new File([blob], fileName, { type: 'application/json' })
  if (navigator.canShare?.({ files: [file] })) {
    try {
      await navigator.share({ files: [file], title: 'MirEfir', text: 'Резервная копия MirEfir' })
      return 'shared'
    } catch (err) {
      if (err.name === 'AbortError') return 'cancel'
    }
  }
  const url = URL.createObjectURL(blob)
  const link = document.createElement('a')
  link.href = url
  link.download = fileName
  link.rel = 'noopener'
  document.body.appendChild(link)
  link.click()
  document.body.removeChild(link)
  window.setTimeout(() => URL.revokeObjectURL(url), 1000)
  return 'downloaded'
}

export async function readBackupPayload(file) {
  if (!file) throw new Error('Файл не выбран')
  if (typeof file === 'string') return JSON.parse(file)
  if (typeof file.text === 'function') return JSON.parse(await file.text())
  if (typeof file === 'object') return file
  throw new Error('Файл резервной копии повреждён')
}
