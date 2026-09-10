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
const GRAMMAR_PREFIXES = ['', 'переключи на ', 'включи ']
const LETTER_SOUND = {
  а: 'а',
  б: 'бэ',
  в: 'вэ',
  г: 'гэ',
  д: 'дэ',
  е: 'е',
  ж: 'жэ',
  з: 'зэ',
  и: 'и',
  й: 'и',
  к: 'ка',
  л: 'эль',
  м: 'эм',
  н: 'эн',
  о: 'о',
  п: 'пэ',
  р: 'эр',
  с: 'эс',
  т: 'тэ',
  у: 'у',
  ф: 'эф',
  х: 'ха',
  ц: 'цэ',
  ч: 'че',
  ш: 'ша',
  щ: 'ща',
  э: 'э',
  ю: 'ю',
  я: 'я',
}

function spokenAbbrev(text) {
  const compact = String(text || '')
    .toLowerCase()
    .replace(/ё/g, 'е')
    .replace(/[^a-zа-я]/g, '')
  if (compact.length < 2 || compact.length > 4) return ''
  const parts = [...compact].map((ch) => LETTER_SOUND[ch] || '')
  if (parts.some((part) => !part)) return ''
  return parts.join(' ')
}

function editDistance(left, right) {
  if (left === right) return 0
  if (!left || !right) return 99
  if (Math.abs(left.length - right.length) > 2) return 99
  const rows = Array.from({ length: left.length + 1 }, (_, i) => {
    const row = new Array(right.length + 1)
    row[0] = i
    return row
  })
  for (let j = 0; j <= right.length; j += 1) rows[0][j] = j
  for (let i = 1; i <= left.length; i += 1) {
    for (let j = 1; j <= right.length; j += 1) {
      const cost = left[i - 1] === right[j - 1] ? 0 : 1
      rows[i][j] = Math.min(rows[i - 1][j] + 1, rows[i][j - 1] + 1, rows[i - 1][j - 1] + cost)
    }
  }
  return rows[left.length][right.length]
}

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

export function channelsForVoice(channels, extras = {}) {
  const out = []
  const seen = new Set()
  const add = (channel) => {
    if (!channel?.id || seen.has(channel.id)) return
    seen.add(channel.id)
    out.push(channel)
  }
  const byId = new Map((channels || []).map((channel) => [channel.id, channel]))
  for (const id of extras.favoriteIds || []) add(byId.get(id))
  for (const channel of channels || []) add(channel)
  return out
}

export function voicePhrasesForChannels(channels, channelLimit = 300) {
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
  let used = 0
  for (const channel of channels || []) {
    const before = phrases.length
    const stripped = cleanPhrase(channel.displayName || channel.name || '').replace(/\s+(UHD|FHD|HD|SD|4K|HEVC|HDR)$/i, '').trim()
    add(stripped)
    add(channel.tvgName)
    const withWords = stripped.replace(/\b([1-9])\b/g, (digit) => NUMBER_WORDS[digit] || digit)
    if (withWords !== stripped) add(withWords)
    add(spokenAbbrev(stripped))
    if (phrases.length > before) used += 1
    if (used >= channelLimit) break
  }
  return phrases
}

export function voskGrammarPhrases(channelPhrases, limit = 960) {
  const seen = new Set()
  const phrases = []
  for (const raw of channelPhrases || []) {
    const base = cleanPhrase(raw).toLowerCase()
    if (base.length < 2) continue
    for (const prefix of GRAMMAR_PREFIXES) {
      const phrase = `${prefix}${base}`
      if (seen.has(phrase)) continue
      seen.add(phrase)
      phrases.push(phrase)
      if (phrases.length >= limit) return phrases.concat('[unk]')
    }
  }
  if (phrases.length) phrases.push('[unk]')
  return phrases
}

function scoreChannel(channel, query, queryParts) {
  const names = [channel.displayName, channel.name, channel.tvgName].map(foldSearch).filter(Boolean)
  let score = 0
  for (const name of names) {
    if (name === query) score = Math.max(score, 120 - Math.min(20, name.length))
    else if (name.startsWith(query) || (query.startsWith(name) && name.length >= 3)) score = Math.max(score, 86)
    else if (name.includes(query) || (query.includes(name) && name.length >= 3)) score = Math.max(score, 72)
    else if (queryParts.length >= 2 && queryParts.every((part) => name.includes(part))) score = Math.max(score, 70)
    else if (name.length >= 4 && query.length >= 4 && editDistance(name, query) <= 1) score = Math.max(score, 68)
  }
  return score
}

function pickFromList(list, query) {
  if (!query) return null
  const queryParts = query.split(' ').filter((part) => part.length >= 2)
  let best = null
  let bestScore = 0
  for (const channel of list || []) {
    const score = scoreChannel(channel, query, queryParts)
    if (score > bestScore) {
      best = channel
      bestScore = score
    }
  }
  return bestScore >= 60 ? best : null
}

export function pickChannelByVoice(channels, spoken, favoriteIds = []) {
  const query = foldSearch(spoken)
  if (!query) return null
  const favSet = new Set(favoriteIds || [])
  const favorites = (channels || []).filter((channel) => favSet.has(channel.id))
  const rest = (channels || []).filter((channel) => !favSet.has(channel.id))
  return pickFromList(favorites, query) || pickFromList(rest, query)
}
