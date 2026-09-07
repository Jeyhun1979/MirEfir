import { APP_VERSION, githubLatestApi, githubReleasesUrl } from './appInfo.js'

const SKIP_KEY = 'mirefir.skipVersion'

function parseVersion(value) {
  return String(value || '')
    .replace(/^v/i, '')
    .split('.')
    .map((part) => Number.parseInt(part, 10) || 0)
}

export function pickDownload(assets, kind = '') {
  const ua = navigator.userAgent
  const files = assets || []
  if (kind === 'android' || /Android/i.test(ua)) {
    return files.find((item) => /\.apk$/i.test(item.name))
  }
  if (kind === 'linux' || (/Linux/i.test(ua) && !/Android/i.test(ua))) {
    return files.find((item) => /\.AppImage$/i.test(item.name)) || files.find((item) => /\.tar\.gz$/i.test(item.name))
  }
  if (kind === 'portable') {
    return files.find((item) => /\.exe$/i.test(item.name) && !/setup/i.test(item.name))
  }
  return files.find((item) => /\.exe$/i.test(item.name) && /setup/i.test(item.name)) || files.find((item) => /\.exe$/i.test(item.name))
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

export async function checkForUpdate(kind = '') {
  const api = githubLatestApi()
  if (!api) return null
  try {
    const response = await fetch(api, { headers: { Accept: 'application/vnd.github+json' } })
    if (!response.ok) return null
    const data = await response.json()
    const version = String(data.tag_name || data.name || '').replace(/^v/i, '')
    if (!version || !isNewerVersion(version) || localStorage.getItem(SKIP_KEY) === version) return null
    const asset = pickDownload(data.assets || [], kind)
    return {
      version,
      notes: data.body || '',
      pageUrl: data.html_url || githubReleasesUrl(),
      downloadUrl: asset?.browser_download_url || '',
    }
  } catch {
    return null
  }
}
