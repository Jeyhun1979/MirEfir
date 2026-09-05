import { APP_VERSION, githubLatestApi, githubReleasesUrl } from './appInfo.js'

const SKIP_KEY = 'mirefir.skipVersion'

function parseVersion(value) {
  return String(value || '')
    .replace(/^v/i, '')
    .split('.')
    .map((part) => Number.parseInt(part, 10) || 0)
}

function pickDownload(assets) {
  const ua = navigator.userAgent
  if (/Linux/i.test(ua) && !/Android/i.test(ua)) {
    return assets.find((item) => /\.AppImage$/i.test(item.name)) || assets.find((item) => /\.tar\.gz$/i.test(item.name))
  }
  if (/Mac/i.test(ua)) {
    return assets.find((item) => /\.dmg$/i.test(item.name))
  }
  return (
    assets.find((item) => /\.exe$/i.test(item.name) && !/setup/i.test(item.name)) ||
    assets.find((item) => /\.exe$/i.test(item.name))
  )
}

export function isNewerVersion(remote, local = APP_VERSION) {
  const a = parseVersion(remote)
  const b = parseVersion(local)
  for (let i = 0; i < Math.max(a.length, b.length); i += 1) {
    if ((a[i] || 0) > (b[i] || 0)) return true
    if ((a[i] || 0) < (b[i] || 0)) return false
  }
  return false
}

export function skipUpdate(version) {
  localStorage.setItem(SKIP_KEY, version)
}

export async function checkForUpdate() {
  const api = githubLatestApi()
  if (!api) return null
  try {
    const response = await fetch(api, { headers: { Accept: 'application/vnd.github+json' } })
    if (!response.ok) return null
    const data = await response.json()
    const version = String(data.tag_name || data.name || '').replace(/^v/i, '')
    if (!version || !isNewerVersion(version) || localStorage.getItem(SKIP_KEY) === version) return null
    const asset = pickDownload(data.assets || [])
    return {
      version,
      notes: data.body || '',
      pageUrl: data.html_url || githubReleasesUrl(),
      downloadUrl: asset?.browser_download_url || data.html_url || githubReleasesUrl(),
    }
  } catch {
    return null
  }
}
