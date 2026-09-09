export const APP_VERSION = '1.0.11'
export const GITHUB_OWNER = 'Jeyhun1979'
export const GITHUB_REPO = 'MirEfir'

export function githubReleasesUrl() {
  if (!GITHUB_OWNER) return ''
  return `https://github.com/${GITHUB_OWNER}/${GITHUB_REPO}/releases`
}

export function githubLatestApi() {
  if (!GITHUB_OWNER) return ''
  return `https://api.github.com/repos/${GITHUB_OWNER}/${GITHUB_REPO}/releases/latest`
}
