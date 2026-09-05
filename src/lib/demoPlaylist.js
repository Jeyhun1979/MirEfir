const MUX = 'https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8'
const APPLE = 'https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_fmp4/master.m3u8'
const TEARS = 'https://demo.unified-streaming.com/k8s/features/stable/video/tears-of-steel/tears-of-steel.ism/.m3u8'
const BIPBOP = 'https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8'

const ICON = {
  news: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/cnn.png',
  world: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/bbc-iplayer.png',
  sport: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/espn.png',
  football: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/fifa.png',
  cinema: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/netflix.png',
  night: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/plex.png',
  planet: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/discovery.png',
  science: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/nasa.png',
  kids: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/disney-plus.png',
  music: 'https://cdn.jsdelivr.net/gh/homarr-labs/dashboard-icons/png/spotify.png',
}

export const DEMO_M3U = `#EXTM3U url-tvg=""
#EXTINF:-1 tvg-id="news-1" tvg-chno="1" tvg-logo="${ICON.news}" group-title="Новости",OneNews HD
${MUX}
#EXTINF:-1 tvg-id="news-2" tvg-chno="2" tvg-logo="${ICON.world}" group-title="Новости",World Desk
${APPLE}
#EXTINF:-1 tvg-id="news-3" tvg-chno="3" group-title="Новости",City 24
${BIPBOP}
#EXTINF:-1 tvg-id="sport-1" tvg-chno="11" tvg-logo="${ICON.sport}" group-title="Спорт",Arena Sport
${TEARS}
#EXTINF:-1 tvg-id="sport-2" tvg-chno="12" tvg-logo="${ICON.football}" group-title="Спорт",Football One
${MUX}
#EXTINF:-1 tvg-id="sport-3" tvg-chno="13" group-title="Спорт",Slow Motion
${APPLE}
#EXTINF:-1 tvg-id="movie-1" tvg-chno="21" tvg-logo="${ICON.cinema}" group-title="Фильмы",Cinema Premiere
${TEARS}
#EXTINF:-1 tvg-id="movie-2" tvg-chno="22" tvg-logo="${ICON.night}" group-title="Фильмы",Night Session
${APPLE}
#EXTINF:-1 tvg-id="movie-3" tvg-chno="23" group-title="Фильмы",Classic Hall
${BIPBOP}
#EXTINF:-1 tvg-id="doc-1" tvg-chno="31" tvg-logo="${ICON.planet}" group-title="Документалистика",Planet X
${APPLE}
#EXTINF:-1 tvg-id="doc-2" tvg-chno="32" tvg-logo="${ICON.science}" group-title="Документалистика",Science 360
${TEARS}
#EXTINF:-1 tvg-id="kids-1" tvg-chno="41" tvg-logo="${ICON.kids}" group-title="Дети",Kids Club
${MUX}
#EXTINF:-1 tvg-id="kids-2" tvg-chno="42" group-title="Дети",Cartoon Box
${BIPBOP}
#EXTINF:-1 tvg-id="music-1" tvg-chno="51" tvg-logo="${ICON.music}" group-title="Музыка",Pulse TV
${APPLE}
#EXTINF:-1 tvg-id="music-2" tvg-chno="52" group-title="Музыка",Retro Hits
${MUX}
`
