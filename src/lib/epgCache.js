import { xmltvWindow } from './settingsStore.js'

async function gzipBytes(bytes) {
  if (typeof CompressionStream !== 'function') return null
  const stream = new Blob([bytes]).stream().pipeThrough(new CompressionStream('gzip'))
  return new Uint8Array(await new Response(stream).arrayBuffer())
}

async function gunzipBytes(bytes) {
  if (typeof DecompressionStream !== 'function') return null
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream('gzip'))
  return new Uint8Array(await new Response(stream).arrayBuffer())
}

export function epgCacheAgeMs(meta) {
  const savedAt = Number(meta?.savedAt) || 0
  if (!savedAt) return Number.POSITIVE_INFINITY
  return Math.max(0, Date.now() - savedAt)
}

export function epgCacheIsFresh(meta, settings) {
  if (!meta?.savedAt) return false
  const span = xmltvWindow(settings)
  const windowOk = Number(meta.backDays) === Number(span.backDays) && Number(meta.aheadDays) === Number(span.aheadDays)
  if (!windowOk) return false
  if (settings?.epgAutoUpdate === false) return true
  const hours = Math.max(1, Number(settings?.epgUpdateHours) || 6)
  return epgCacheAgeMs(meta) < hours * 60 * 60 * 1000
}

export async function readEpgCacheMeta() {
  if (!window.mirefir?.epgCacheMeta) return null
  try {
    return await window.mirefir.epgCacheMeta()
  } catch {
    return null
  }
}

export async function loadEpgCache() {
  if (!window.mirefir?.loadEpgCache) return null
  try {
    const raw = await window.mirefir.loadEpgCache()
    if (!raw) return null
    const bytes = raw instanceof ArrayBuffer ? new Uint8Array(raw) : new Uint8Array(raw)
    const unzipped = await gunzipBytes(bytes)
    if (!unzipped) return null
    const parsed = JSON.parse(new TextDecoder().decode(unzipped))
    if (!parsed?.programs || typeof parsed.programs !== 'object') return null
    return parsed
  } catch {
    return null
  }
}

export async function saveEpgCache(xmltv, meta) {
  if (!window.mirefir?.saveEpgCache || !xmltv?.programs) return false
  try {
    const json = JSON.stringify({
      channels: xmltv.channels || {},
      programs: xmltv.programs || {},
    })
    const gz = await gzipBytes(new TextEncoder().encode(json))
    if (!gz) return false
    await window.mirefir.saveEpgCache({
      meta: {
        savedAt: Date.now(),
        url: meta?.url || '',
        backDays: Number(meta?.backDays) || 7,
        aheadDays: Number(meta?.aheadDays) || 7,
      },
      bytes: gz,
    })
    return true
  } catch {
    return false
  }
}

export function uniqueEpgUrls(url) {
  const list = (Array.isArray(url) ? url : [url]).map((item) => String(item || '').trim()).filter(Boolean)
  const out = []
  for (const item of list) {
    if (!out.includes(item)) out.push(item)
  }
  return out.slice(0, 2)
}
