import { useEffect, useRef, useState } from 'react'
import { APP_VERSION } from '../../lib/appInfo.js'
import { copyText, shareCloudCode } from '../../lib/cloudCode.js'
import { eventToken, keyCaption } from '../../lib/remoteKeys.js'
import { ARCHIVE_DAYS, CLOCK_POSITIONS, CLOCK_SIZES, DEFAULT_SETTINGS, KEY_LABELS, saveSettings } from '../../lib/settingsStore.js'
import { pickStorageFolder } from '../../lib/storage.js'
import { usePlayer } from '../../store/PlayerContext.jsx'

const TABS = [
  { id: 'playlists', title: 'Плейлисты' },
  { id: 'epg', title: 'Телепрограмма' },
  { id: 'appearance', title: 'Внешний вид' },
  { id: 'player', title: 'Плеер' },
  { id: 'recordings', title: 'Записи' },
  { id: 'archive', title: 'Архив' },
  { id: 'parental', title: 'Родительский контроль' },
  { id: 'remote', title: 'Пульт' },
  { id: 'language', title: 'Язык' },
  { id: 'data', title: 'Данные' },
  { id: 'about', title: 'О программе' },
]

function Row({ title, hint, children }) {
  return (
    <div className="flex items-center justify-between gap-4 border-b border-white/5 py-3">
      <div>
        <div className="text-[14px]">{title}</div>
        {hint ? <div className="text-[12px] text-white/35">{hint}</div> : null}
      </div>
      <div className="shrink-0">{children}</div>
    </div>
  )
}

function Toggle({ value, onChange }) {
  return (
    <button
      type="button"
      onClick={() => onChange(!value)}
      className={`rounded-full px-3 py-1 text-xs ${value ? 'bg-accent' : 'bg-white/10 text-white/50'}`}
    >
      {value ? 'Вкл' : 'Выкл'}
    </button>
  )
}

export function SettingsScreen() {
  const {
    uiScreen,
    settingsTab,
    setSettingsTab,
    closeOverlays,
    openMenu,
    settings,
    updateSettings,
    playlistUrl,
    epgUrl,
    playlistGroups,
    importFromUrl,
    importEpg,
    exportBackup,
    exportCloudCode,
    importCloudCode,
    importBackupFile,
    setError,
    status,
  } = usePlayer()

  const [playlistDraft, setPlaylistDraft] = useState('')
  const [epgDraft, setEpgDraft] = useState('')
  const [xtream, setXtream] = useState({ server: '', user: '', pass: '' })
  const [pinDraft, setPinDraft] = useState('')
  const [pinUnlock, setPinUnlock] = useState('')
  const [unlocked, setUnlocked] = useState(false)
  const [busy, setBusy] = useState('')
  const [capturing, setCapturing] = useState('')
  const restoreRef = useRef(null)
  const [cloudCode, setCloudCode] = useState('')
  const [cloudNote, setCloudNote] = useState('')

  useEffect(() => {
    setPlaylistDraft(playlistUrl || '')
    setEpgDraft(settings.epgUrl || epgUrl || '')
  }, [epgUrl, playlistUrl, settings.epgUrl, uiScreen])

  useEffect(() => {
    if (!capturing) return undefined
    const onKey = (event) => {
      event.preventDefault()
      event.stopPropagation()
      updateSettings({
        keys: { ...settings.keys, [capturing]: eventToken(event) },
      })
      setCapturing('')
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
  }, [capturing, settings.keys, updateSettings])

  if (uiScreen !== 'settings') return null

  if (settings.lockSettings && settings.parentalEnabled && !unlocked) {
    return (
      <div className="absolute inset-0 z-50 flex items-center justify-center bg-[#070b12]">
        <div className="w-full max-w-sm rounded-2xl border border-white/10 bg-[#10151e] p-6">
          <div className="mb-3 text-lg font-semibold">Родительский контроль</div>
          <p className="mb-3 text-sm text-white/45">Введите PIN, чтобы открыть настройки.</p>
          <input
            value={pinUnlock}
            onChange={(event) => setPinUnlock(event.target.value)}
            maxLength={8}
            className="mb-3 w-full rounded-xl border border-white/10 bg-black/30 px-3 py-2.5 text-sm"
            placeholder="PIN"
          />
          <div className="flex gap-2">
            <button type="button" className="rounded-lg bg-accent px-4 py-2 text-sm" onClick={() => setUnlocked(pinUnlock === settings.parentalPin)}>
              Войти
            </button>
            <button type="button" className="rounded-lg bg-white/10 px-4 py-2 text-sm" onClick={openMenu}>
              Назад
            </button>
          </div>
        </div>
      </div>
    )
  }

  const cycle = (list, current, key) => {
    const index = Math.max(0, list.findIndex((item) => item === current || item.id === current))
    const next = list[(index + 1) % list.length]
    updateSettings({ [key]: next.id || next })
  }

  return (
    <div className="absolute inset-0 z-50 flex bg-[#070b12]">
      <aside className="flex w-[260px] flex-col border-r border-white/10 bg-[#0c111a]">
        <button type="button" onClick={openMenu} className="remote-hit px-5 text-left text-sm text-white/50 hover:text-white">
          ‹ Меню
        </button>
        <div className="scroll-thin flex-1 overflow-y-auto pb-4">
          {TABS.map((tab) => (
            <button
              key={tab.id}
              type="button"
              onClick={() => setSettingsTab(tab.id)}
              className={`remote-hit w-full px-5 text-left text-[14px] ${
                settingsTab === tab.id ? 'bg-accent text-white' : 'text-white/65 hover:bg-white/5'
              }`}
            >
              {tab.title}
            </button>
          ))}
        </div>
      </aside>

      <section className="scroll-thin min-w-0 flex-1 overflow-y-auto px-8 py-6">
        <div className="mb-6 flex items-start justify-between">
          <div>
            <div className="text-2xl font-semibold">{TABS.find((tab) => tab.id === settingsTab)?.title}</div>
            <div className="text-sm text-white/40">{status}</div>
          </div>
          <button type="button" onClick={closeOverlays} className="text-sm text-white/45">
            Закрыть
          </button>
        </div>

        {settingsTab === 'playlists' ? (
          <div>
            <Row title="Текущий плейлист" hint="M3U / M3U8 ссылка">
              <span className="max-w-xs truncate text-xs text-white/45">{playlistUrl}</span>
            </Row>
            <input
              value={playlistDraft}
              onChange={(event) => setPlaylistDraft(event.target.value)}
              className="mt-3 w-full rounded-xl border border-white/10 bg-black/30 px-3 py-2.5 text-sm outline-none focus:border-accent"
            />
            <button
              type="button"
              disabled={busy || !playlistDraft.trim()}
              onClick={async () => {
                setBusy('playlist')
                try {
                  await importFromUrl(playlistDraft.trim())
                  updateSettings({
                    playlists: [{ id: 'default', name: 'Основной', url: playlistDraft.trim() }],
                    activePlaylistId: 'default',
                  })
                } catch (err) {
                  setError(err.message)
                } finally {
                  setBusy('')
                }
              }}
              className="mt-3 rounded-xl bg-accent px-4 py-2 text-sm"
            >
              {busy === 'playlist' ? 'Загрузка…' : 'Обновить плейлист'}
            </button>
            <div className="mt-8 text-sm font-medium text-white/70">Xtream Codes</div>
            <p className="mb-2 text-xs text-white/35">Адрес сервера, логин и пароль от вашей подписки.</p>
            <input className="mb-2 w-full rounded-xl border border-white/10 bg-black/30 px-3 py-2 text-sm" placeholder="http://server:port" value={xtream.server} onChange={(e) => setXtream({ ...xtream, server: e.target.value })} />
            <div className="mb-2 flex gap-2">
              <input className="flex-1 rounded-xl border border-white/10 bg-black/30 px-3 py-2 text-sm" placeholder="Логин" value={xtream.user} onChange={(e) => setXtream({ ...xtream, user: e.target.value })} />
              <input className="flex-1 rounded-xl border border-white/10 bg-black/30 px-3 py-2 text-sm" placeholder="Пароль" type="password" value={xtream.pass} onChange={(e) => setXtream({ ...xtream, pass: e.target.value })} />
            </div>
            <button
              type="button"
              className="rounded-xl bg-white/10 px-4 py-2 text-sm"
              onClick={async () => {
                const base = xtream.server.replace(/\/$/, '')
                const url = `${base}/get.php?username=${encodeURIComponent(xtream.user)}&password=${encodeURIComponent(xtream.pass)}&type=m3u_plus`
                setBusy('playlist')
                try {
                  await importFromUrl(url)
                } catch (err) {
                  setError(err.message)
                } finally {
                  setBusy('')
                }
              }}
            >
              Добавить Xtream
            </button>
            <Row title="Интервал обновления" hint="Как часто перечитывать M3U">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([1, 3, 6, 12, 24], settings.playlistUpdateHours, 'playlistUpdateHours')}>
                {settings.playlistUpdateHours} ч
              </button>
            </Row>
            <Row title="User-Agent">
              <input className="w-56 rounded-lg border border-white/10 bg-black/30 px-2 py-1 text-xs" value={settings.userAgent} onChange={(e) => updateSettings({ userAgent: e.target.value })} />
            </Row>
            <div className="mt-6 text-sm font-medium text-white/70">Скрыть группы</div>
            <div className="mt-2 max-h-48 overflow-y-auto rounded-xl border border-white/5 p-2">
              {(playlistGroups || []).map((group) => {
                const hidden = (settings.hiddenGroups || []).includes(group.id)
                return (
                  <button
                    key={group.id}
                    type="button"
                    className="flex w-full items-center justify-between rounded-lg px-2 py-1.5 text-left text-sm hover:bg-white/5"
                    onClick={() => {
                      const current = settings.hiddenGroups || []
                      updateSettings({
                        hiddenGroups: hidden ? current.filter((id) => id !== group.id) : [...current, group.id],
                      })
                    }}
                  >
                    <span>{group.name}</span>
                    <span className="text-xs text-white/35">{hidden ? 'скрыта' : 'видима'}</span>
                  </button>
                )
              })}
            </div>
            <Row title="Сортировка каналов">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['playlist', 'name', 'number'], settings.channelSort, 'channelSort')}>
                {settings.channelSort === 'name' ? 'По имени' : settings.channelSort === 'number' ? 'По номеру' : 'Как в файле'}
              </button>
            </Row>
          </div>
        ) : null}

        {settingsTab === 'epg' ? (
          <div>
            <Row title="Источник XMLTV" hint=".xml или .xml.gz" />
            <input
              value={epgDraft}
              onChange={(event) => setEpgDraft(event.target.value)}
              className="mb-3 w-full rounded-xl border border-white/10 bg-black/30 px-3 py-2.5 text-sm outline-none focus:border-accent"
            />
            <button
              type="button"
              disabled={busy === 'epg'}
              onClick={async () => {
                setBusy('epg')
                try {
                  await importEpg(epgDraft.trim())
                  updateSettings({ epgUrl: epgDraft.trim() })
                } catch (err) {
                  setError(err.message)
                } finally {
                  setBusy('')
                }
              }}
              className="mb-4 rounded-xl bg-accent px-4 py-2 text-sm"
            >
              {busy === 'epg' ? 'Загрузка EPG…' : 'Загрузить телепрограмму'}
            </button>
            <Row title="Сдвиг времени" hint="Если передачи идут раньше или позже">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => updateSettings({ epgOffsetHours: ((settings.epgOffsetHours + 1 + 12) % 25) - 12 })}>
                {settings.epgOffsetHours > 0 ? `+${settings.epgOffsetHours} ч` : `${settings.epgOffsetHours} ч`}
              </button>
            </Row>
            <Row title="Хранить программу" hint="Сколько дней держать в гиде">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([1, 2, 3, 5, 7], settings.epgDays, 'epgDays')}>
                {settings.epgDays} дн.
              </button>
            </Row>
            <Row title="Автообновление EPG">
              <Toggle value={settings.epgAutoUpdate} onChange={(value) => updateSettings({ epgAutoUpdate: value })} />
            </Row>
            <Row title="Интервал обновления EPG">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([3, 6, 12, 24], settings.epgUpdateHours, 'epgUpdateHours')}>
                {settings.epgUpdateHours} ч
              </button>
            </Row>
            <Row title="Описания передач">
              <Toggle value={settings.showProgramDesc} onChange={(value) => updateSettings({ showProgramDesc: value })} />
            </Row>
          </div>
        ) : null}

        {settingsTab === 'player' ? (
          <div>
            <Row title="Автозапуск канала">
              <Toggle value={settings.autoplay} onChange={(value) => updateSettings({ autoplay: value })} />
            </Row>
            <Row title="Запоминать громкость">
              <Toggle value={settings.rememberVolume} onChange={(value) => updateSettings({ rememberVolume: value })} />
            </Row>
            <Row title="Подтверждать выход" hint="Esc из меню не сразу закрывает">
              <Toggle value={settings.confirmExit} onChange={(value) => updateSettings({ confirmExit: value })} />
            </Row>
            <Row title="Декодер">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['auto', 'hardware', 'software'], settings.decoder, 'decoder')}>
                {settings.decoder}
              </button>
            </Row>
            <Row title="Буфер" hint="Секунд предзагрузки">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([8, 15, 30, 60], settings.bufferSec, 'bufferSec')}>
                {settings.bufferSec} с
              </button>
            </Row>
            <Row title="Соотношение сторон">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['auto', '16:9', '4:3', 'zoom'], settings.aspect, 'aspect')}>
                {settings.aspect}
              </button>
            </Row>
            <Row title="Внешний плеер">
              <Toggle value={settings.externalPlayer} onChange={(value) => updateSettings({ externalPlayer: value })} />
            </Row>
            <Row title="Язык дорожки по умолчанию">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['ru', 'en', 'orig'], settings.defaultAudio, 'defaultAudio')}>
                {settings.defaultAudio}
              </button>
            </Row>
          </div>
        ) : null}

        {settingsTab === 'recordings' ? (
          <div>
            <Row title="Запись передач" hint="Пишем только на внешний диск или флешку">
              <Toggle value={settings.recordingsEnabled} onChange={(value) => updateSettings({ recordingsEnabled: value })} />
            </Row>
            <Row title="Папка записи" hint={settings.recordingPath || 'Спросим при первой записи'}>
              <div className="flex gap-2">
                {settings.recordingPath ? (
                  <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => updateSettings({ recordingPath: '' })}>
                    Спросить снова
                  </button>
                ) : null}
                <button
                  type="button"
                  className="rounded-lg bg-accent px-3 py-1 text-sm"
                  onClick={async () => {
                    try {
                      const picked = await pickStorageFolder()
                      if (picked?.path) updateSettings({ recordingPath: picked.path, recordingsEnabled: true })
                    } catch (err) {
                      window.alert(err.message)
                    }
                  }}
                >
                  Выбрать папку
                </button>
              </div>
            </Row>
            <p className="mt-4 text-sm text-white/40">
              При записи спросим: внутренняя память или диск/флешка. Если путь уже выбран — пишем туда сразу.
            </p>
          </div>
        ) : null}

        {settingsTab === 'archive' ? (
          <div>
            <Row title="Архив (catch-up)" hint="Каналы с tvg-rec из плейлиста">
              <Toggle value={settings.archiveEnabled} onChange={(value) => updateSettings({ archiveEnabled: value })} />
            </Row>
            <Row title="Глубина архива" hint="1, 3, 5, 7 или 14 дней">
              <div className="flex gap-2">
                {ARCHIVE_DAYS.map((days) => (
                  <button
                    key={days}
                    type="button"
                    onClick={() => updateSettings({ archiveDays: days, archiveEnabled: true })}
                    className={`rounded-lg px-3 py-1 text-sm ${settings.archiveDays === days ? 'bg-accent' : 'bg-white/10'}`}
                  >
                    {days}
                  </button>
                ))}
              </div>
            </Row>
            <p className="mt-4 text-sm text-white/40">
              В меню «Архив» показываются каналы, у которых в плейлисте указан catch-up (`tvg-rec`). Глубина
              ограничивает, сколько дней записи считать доступными.
            </p>
          </div>
        ) : null}

        {settingsTab === 'appearance' ? (
          <div>
            <Row title="Тема">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['dark', 'oled', 'graphite'], settings.theme, 'theme')}>
                {settings.theme}
              </button>
            </Row>
            <Row title="Цвет выделения">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['blue', 'orange', 'teal'], settings.highlight, 'highlight')}>
                {settings.highlight === 'orange' ? 'Оранжевый' : settings.highlight === 'teal' ? 'Бирюзовый' : 'Синий'}
              </button>
            </Row>
            <Row title="Прозрачность панелей">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([70, 80, 90, 100], settings.panelOpacity, 'panelOpacity')}>
                {settings.panelOpacity}%
              </button>
            </Row>
            <Row title="Размер шрифта">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['sm', 'md', 'lg'], settings.fontSize, 'fontSize')}>
                {settings.fontSize}
              </button>
            </Row>
            <Row title="Размер логотипов">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['sm', 'md', 'lg'], settings.logoSize, 'logoSize')}>
                {settings.logoSize}
              </button>
            </Row>
            <Row title="Часов в телепрограмме" hint="Ширина сетки EPG">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([3, 4, 6, 12, 18], settings.guideHours, 'guideHours')}>
                {settings.guideHours} ч
              </button>
            </Row>
            <Row title="Строк в гиде">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([4, 5, 6, 7, 8, 9], settings.guideRows, 'guideRows')}>
                {settings.guideRows}
              </button>
            </Row>
            <Row title="Название передачи в две строки">
              <Toggle value={settings.twoLineTitles} onChange={(value) => updateSettings({ twoLineTitles: value })} />
            </Row>
            <Row title="Часы на экране">
              <Toggle value={settings.clockEnabled} onChange={(value) => updateSettings({ clockEnabled: value })} />
            </Row>
            <Row title="Положение часов">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(CLOCK_POSITIONS, settings.clockPosition, 'clockPosition')}>
                {CLOCK_POSITIONS.find((item) => item.id === settings.clockPosition)?.name}
              </button>
            </Row>
            <Row title="Размер часов">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(CLOCK_SIZES, settings.clockSize, 'clockSize')}>
                {CLOCK_SIZES.find((item) => item.id === settings.clockSize)?.name}
              </button>
            </Row>
            <Row title="Прозрачность часов">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle([60, 75, 90, 100], settings.clockOpacity, 'clockOpacity')}>
                {settings.clockOpacity}%
              </button>
            </Row>
            <Row title="Номера каналов">
              <Toggle value={settings.showChannelNumbers} onChange={(value) => updateSettings({ showChannelNumbers: value })} />
            </Row>
            <Row title="Полоса прогресса в списке">
              <Toggle value={settings.showEpgProgress} onChange={(value) => updateSettings({ showEpgProgress: value })} />
            </Row>
          </div>
        ) : null}

        {settingsTab === 'parental' ? (
          <div>
            <Row title="Родительский контроль">
              <Toggle value={settings.parentalEnabled} onChange={(value) => updateSettings({ parentalEnabled: value })} />
            </Row>
            <Row title="Закрыть настройки PIN-кодом">
              <Toggle value={settings.lockSettings} onChange={(value) => updateSettings({ lockSettings: value })} />
            </Row>
            <Row title="Сменить PIN">
              <div className="flex gap-2">
                <input className="w-28 rounded-lg border border-white/10 bg-black/30 px-2 py-1 text-sm" maxLength={8} value={pinDraft} onChange={(e) => setPinDraft(e.target.value)} placeholder="новый PIN" />
                <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => pinDraft && updateSettings({ parentalPin: pinDraft })}>
                  Сохранить
                </button>
              </div>
            </Row>
            <div className="mt-4 text-sm text-white/40">Заблокированные группы</div>
            {(playlistGroups || []).map((group) => {
              const locked = (settings.lockedGroups || []).includes(group.id)
              return (
                <button
                  key={group.id}
                  type="button"
                  className="flex w-full items-center justify-between border-b border-white/5 py-2 text-left text-sm"
                  onClick={() => {
                    const current = settings.lockedGroups || []
                    updateSettings({
                      lockedGroups: locked ? current.filter((id) => id !== group.id) : [...current, group.id],
                    })
                  }}
                >
                  <span>{group.name}</span>
                  <span className="text-xs text-white/35">{locked ? 'PIN' : 'открыта'}</span>
                </button>
              )
            })}
          </div>
        ) : null}

        {settingsTab === 'language' ? (
          <div>
            <Row title="Язык интерфейса">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => cycle(['ru', 'en'], settings.language, 'language')}>
                {settings.language === 'en' ? 'English' : 'Русский'}
              </button>
            </Row>
          </div>
        ) : null}

        {settingsTab === 'remote' ? (
          <div>
            <p className="mb-4 text-sm text-white/40">
              Нажмите «Сменить», затем кнопку на пульте (G10S, D-pad, гиромышь OK). Назад, CH+/CH−, поиск и гид работают сразу. Громкость: + и −.
            </p>
            {Object.keys(KEY_LABELS).map((id) => (
              <Row key={id} title={KEY_LABELS[id]} hint={capturing === id ? 'Нажмите кнопку на пульте…' : keyCaption(settings.keys[id])}>
                <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => setCapturing(id)}>
                  Сменить
                </button>
              </Row>
            ))}
          </div>
        ) : null}

        {settingsTab === 'data' ? (
          <div>
            <div className="mb-4 rounded-xl border border-white/10 bg-black/20 p-3">
              <div className="mb-1 text-sm">Облачный код</div>
              <p className="mb-3 text-xs text-white/40">
                Код работает в обе стороны: Windows ↔ Android. В нём избранное и ссылки на плейлист и телепрограмму. Локальный файл M3U так не передаётся — нужен URL. Скопируйте или отправьте в мессенджер и вставьте на другом устройстве.
              </p>
              <textarea
                value={cloudCode}
                onChange={(event) => setCloudCode(event.target.value)}
                rows={3}
                placeholder="ME1.…"
                className="mb-2 w-full rounded-lg border border-white/10 bg-black/30 px-2 py-2 font-mono text-xs outline-none"
              />
              <div className="flex flex-wrap gap-2">
                <button
                  type="button"
                  className="rounded-lg bg-accent px-3 py-1 text-sm"
                  onClick={async () => {
                    const code = exportCloudCode()
                    setCloudCode(code)
                    try {
                      await copyText(code)
                      setCloudNote('Код скопирован. Вставьте его в приложении на другом устройстве.')
                    } catch {
                      setCloudNote('Не удалось скопировать автоматически — выделите код и скопируйте вручную')
                    }
                  }}
                >
                  Скопировать код
                </button>
                <button
                  type="button"
                  className="rounded-lg bg-white/10 px-3 py-1 text-sm"
                  onClick={async () => {
                    const code = cloudCode.trim() || exportCloudCode()
                    setCloudCode(code)
                    try {
                      const result = await shareCloudCode(code)
                      if (result === 'shared') setCloudNote('Отправлено в мессенджер')
                      else if (result === 'copied') setCloudNote('Системная отправка недоступна — код скопирован, вставьте в Telegram, WhatsApp и т.д.')
                    } catch (err) {
                      setCloudNote(err.message)
                    }
                  }}
                >
                  Поделиться
                </button>
                <button
                  type="button"
                  className="rounded-lg bg-white/10 px-3 py-1 text-sm"
                  onClick={() => {
                    importCloudCode(cloudCode)
                      .then(() => setCloudNote('Восстановлено'))
                      .catch((err) => setCloudNote(err.message))
                  }}
                >
                  Применить код
                </button>
              </div>
              {cloudNote ? <div className="mt-2 text-xs text-white/50">{cloudNote}</div> : null}
            </div>
            <Row title="Сохранить данные" hint="Полный JSON-файл">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={exportBackup}>
                Экспорт
              </button>
            </Row>
            <Row title="Восстановить данные" hint="JSON-файл резервной копии">
              <button type="button" className="rounded-lg bg-white/10 px-3 py-1 text-sm" onClick={() => restoreRef.current?.click()}>
                Импорт
              </button>
            </Row>
            <Row title="Сбросить настройки">
              <button
                type="button"
                className="rounded-lg bg-live/80 px-3 py-1 text-sm"
                onClick={() => {
                  saveSettings(DEFAULT_SETTINGS)
                  updateSettings(() => ({ ...DEFAULT_SETTINGS }))
                }}
              >
                Сброс
              </button>
            </Row>
            <input
              ref={restoreRef}
              type="file"
              accept="application/json"
              className="hidden"
              onChange={(event) => {
                const file = event.target.files?.[0]
                if (file) importBackupFile(file).catch((err) => setError(err.message))
              }}
            />
          </div>
        ) : null}

        {settingsTab === 'about' ? (
          <div className="space-y-3 text-sm text-white/60">
            <div className="text-xl text-white">MirEfir</div>
            <p>IPTV-плеер: плейлисты, телепрограмма, архив, пульт и резервные копии.</p>
            <p>Версия {APP_VERSION}</p>
          </div>
        ) : null}
      </section>
    </div>
  )
}
