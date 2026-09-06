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

export function encodeCloudCode({ favoriteNames = [], playlists = [], epgUrl = '' }) {
  const payload = {
    v: 1,
    f: favoriteNames.filter(Boolean).slice(0, 200),
    p: playlists.map((item) => (typeof item === 'string' ? item : item.url)).filter(Boolean),
    e: epgUrl || '',
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
  if (!data || data.v !== 1) throw new Error('Неизвестный формат кода')
  return {
    favoriteNames: Array.isArray(data.f) ? data.f : [],
    playlistUrls: Array.isArray(data.p) ? data.p.filter(Boolean) : data.p ? [data.p] : [],
    epgUrl: data.e || '',
  }
}
