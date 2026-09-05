const LOGO_BASE = 'https://cdn.jsdelivr.net/gh/tv-logo/tv-logos@main/countries'

const BY_NAME = {
  'первый канал': `${LOGO_BASE}/russia/1tv-ru.png`,
  'россия 1': `${LOGO_BASE}/russia/russia-1-ru.png`,
  'россия 24': `${LOGO_BASE}/russia/russia-24-ru.png`,
  'россия к': `${LOGO_BASE}/russia/russia-k-ru.png`,
  'нтв': `${LOGO_BASE}/russia/ntv-ru.png`,
  'рен тв': `${LOGO_BASE}/russia/ren-tv-ru.png`,
  'рен-тв': `${LOGO_BASE}/russia/ren-tv-ru.png`,
  'пятый канал': `${LOGO_BASE}/russia/5-tv-ru.png`,
  'тнт': `${LOGO_BASE}/russia/tnt-ru.png`,
  'стс': `${LOGO_BASE}/russia/ctc-ru.png`,
  'тв3': `${LOGO_BASE}/russia/tv3-ru.png`,
  'тв-3': `${LOGO_BASE}/russia/tv3-ru.png`,
  'пятница': `${LOGO_BASE}/russia/friday-ru.png`,
  'пятница!': `${LOGO_BASE}/russia/friday-ru.png`,
  'карусель': `${LOGO_BASE}/russia/karusel-ru.png`,
  'матч тв': `${LOGO_BASE}/russia/match-tv-ru.png`,
  'матч премьер': `${LOGO_BASE}/russia/match-premier-ru.png`,
  'москва 24': `${LOGO_BASE}/russia/moscow-24-ru.png`,
  'рбк': `${LOGO_BASE}/russia/rbk-ru.png`,
  'мир': `${LOGO_BASE}/russia/mir-ru.png`,
  'мир 24': `${LOGO_BASE}/russia/mir-24-ru.png`,
  'звезда': `${LOGO_BASE}/russia/zvezda-ru.png`,
  'спа': `${LOGO_BASE}/russia/spas-ru.png`,
  'домашний': `${LOGO_BASE}/russia/domashniy-ru.png`,
  'че': `${LOGO_BASE}/russia/che-ru.png`,
  'ю': `${LOGO_BASE}/russia/u-ru.png`,
  'муз-тв': `${LOGO_BASE}/russia/muz-tv-ru.png`,
  'муз тв': `${LOGO_BASE}/russia/muz-tv-ru.png`,
  'русский хит': `${LOGO_BASE}/russia/russkiy-hit-ru.png`,
  'санкт-петербург': `${LOGO_BASE}/russia/saint-petersburg-ru.png`,
  '360': `${LOGO_BASE}/russia/360-ru.png`,
  'bbc world news': `${LOGO_BASE}/international/bbc-world-news-int.png`,
  'bbc': `${LOGO_BASE}/international/bbc-world-news-int.png`,
  'cnn': `${LOGO_BASE}/united-states/cnn-us.png`,
  'cnbc': `${LOGO_BASE}/united-states/cnbc-us.png`,
  'france 24': `${LOGO_BASE}/france/france-24-fr.png`,
  'euronews': `${LOGO_BASE}/international/euronews-int.png`,
  'discovery': `${LOGO_BASE}/united-states/discovery-channel-us.png`,
  'animal planet': `${LOGO_BASE}/united-states/animal-planet-us.png`,
  'national geographic': `${LOGO_BASE}/united-states/national-geographic-us.png`,
  'nickelodeon': `${LOGO_BASE}/united-states/nickelodeon-us.png`,
  'cartoon network': `${LOGO_BASE}/united-states/cartoon-network-us.png`,
  'mtv': `${LOGO_BASE}/united-states/mtv-us.png`,
}

function cleanName(name) {
  return String(name || '')
    .toLowerCase()
    .replace(/uhd|fhd|hd|sd|4k|50|hevc/gi, ' ')
    .replace(/\+\d+/g, ' ')
    .replace(/\([^)]*\)/g, ' ')
    .replace(/\b(uk|us|nl|fr|de|ru|рус|россия)\b/gi, ' ')
    .replace(/[|•·_/\\]+/g, ' ')
    .replace(/\s+/g, ' ')
    .trim()
}

export function resolveChannelLogo(name, existing = '') {
  if (existing) return existing
  const key = cleanName(name)
  if (BY_NAME[key]) return BY_NAME[key]

  const words = key.split(' ')
  for (let size = words.length; size > 0; size -= 1) {
    const chunk = words.slice(0, size).join(' ')
    if (BY_NAME[chunk]) return BY_NAME[chunk]
  }

  return ''
}

export function attachLogos(channels) {
  return channels.map((channel) => ({
    ...channel,
    logo: resolveChannelLogo(channel.displayName || channel.name, channel.logo),
  }))
}
