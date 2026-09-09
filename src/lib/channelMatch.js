function stripWords(text, words) {
  const set = new Set(words)
  return text
    .split(' ')
    .filter((word) => word && !set.has(word))
    .join(' ')
}

function cleanPhrase(text) {
  return String(text || '')
    .replace(/ё/gi, 'е')
    .replace(/[^\p{L}\p{N} ]+/gu, ' ')
    .replace(/\s+/g, ' ')
    .trim()
}

const NUMBER_WORDS = {
  1: 'один',
  2: 'два',
  3: 'три',
  4: 'четыре',
  5: 'пять',
  6: 'шесть',
  7: 'семь',
  8: 'восемь',
  9: 'девять',
}

const SWITCH_PREFIX =
  /^(переключи(?:ть)?|включи(?:ть)?|открой(?:те)?|поставь(?:те)?|давай|смотрим)(?:\s+на)?\s+/i
const SEARCH_PREFIX = /^(найди(?:те)?|найти|поиск|ищи|искать)(?:\s+канал)?\s+/i

export function foldSearch(text) {
  let value = String(text || '')
    .toLowerCase()
    .replace(/ё/g, 'е')
    .replace(/[^\p{L}\p{N}]+/gu, ' ')
    .replace(/\s+/g, ' ')
    .trim()
  value = value
    .replace(/(^| )(один|одна)( |$)/g, (_, left, _word, right) => `${left}1${right}`)
    .replace(/(^| )два( |$)/g, (_, left, right) => `${left}2${right}`)
    .replace(/(^| )три( |$)/g, (_, left, right) => `${left}3${right}`)
  value = stripWords(value, ['uhd', 'fhd', 'hd', 'sd', '4k', 'hevc', 'hdr', 'tv', 'тв'])
  return value.replace(/\s+/g, ' ').trim()
}

export function parseVoiceCommand(spoken, grammarName = '') {
  const raw = String(spoken || '').replace(/\s+/g, ' ').trim()
  if (!raw) return { intent: 'search', query: '' }
  if (grammarName === 'switch') {
    return { intent: 'switch', query: raw.replace(SWITCH_PREFIX, '').trim() || raw }
  }
  if (grammarName === 'search') {
    return { intent: 'search', query: raw.replace(SEARCH_PREFIX, '').trim() || raw }
  }
  const switchHit = raw.match(SWITCH_PREFIX)
  if (switchHit) {
    return { intent: 'switch', query: raw.slice(switchHit[0].length).trim() }
  }
  const searchHit = raw.match(SEARCH_PREFIX)
  if (searchHit) {
    return { intent: 'search', query: raw.slice(searchHit[0].length).trim() }
  }
  return { intent: 'search', query: raw }
}

export function voicePhrasesForChannels(channels, limit = 1000) {
  const seen = new Set()
  const phrases = []
  const add = (raw) => {
    const phrase = cleanPhrase(raw)
    if (phrase.length < 2 || phrase.length > 72) return
    const key = phrase.toLowerCase()
    if (seen.has(key)) return
    seen.add(key)
    phrases.push(phrase)
  }
  for (const channel of channels || []) {
    for (const raw of [channel.displayName, channel.name, channel.tvgName]) {
      add(raw)
      const stripped = cleanPhrase(raw).replace(/\s+(UHD|FHD|HD|SD|4K|HEVC|HDR)$/i, '').trim()
      add(stripped)
      const withWords = stripped.replace(/\b([1-9])\b/g, (digit) => NUMBER_WORDS[digit] || digit)
      if (withWords !== stripped) add(withWords)
    }
    if (phrases.length >= limit) break
  }
  return phrases.slice(0, limit)
}

export function pickChannelByVoice(channels, spoken) {
  const query = foldSearch(spoken)
  if (!query) return null
  let best = null
  let bestScore = 0
  for (const channel of channels || []) {
    const names = [channel.displayName, channel.name, channel.tvgName].map(foldSearch).filter(Boolean)
    let score = 0
    for (const name of names) {
      if (name === query) score = Math.max(score, 120 - Math.min(20, name.length))
      else if (name.startsWith(query) || (query.startsWith(name) && name.length >= 3)) score = Math.max(score, 86)
      else if (name.includes(query) || (query.includes(name) && name.length >= 3)) score = Math.max(score, 72)
    }
    if (score > bestScore) {
      best = channel
      bestScore = score
    }
  }
  return bestScore >= 60 ? best : null
}
