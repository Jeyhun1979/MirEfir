import { useEffect } from 'react'
import { App as CapApp } from '@capacitor/app'
import { Capacitor } from '@capacitor/core'
import { StatusBar, Style } from '@capacitor/status-bar'
import { ChannelList } from './components/ChannelList.jsx'
import { EpgTimeline } from './components/EpgTimeline.jsx'
import { PlaylistModal } from './components/PlaylistModal.jsx'
import { UpdateDialog } from './components/UpdateDialog.jsx'
import { MainMenu } from './components/menu/MainMenu.jsx'
import { SearchOverlay } from './components/menu/SearchOverlay.jsx'
import { MultiViewScreen, RecordingsScreen } from './components/menu/ExtraScreens.jsx'
import { SettingsScreen } from './components/menu/SettingsScreen.jsx'
import { Sidebar } from './components/Sidebar.jsx'
import { TopBar } from './components/TopBar.jsx'
import { LiveGuideOverlay } from './components/LiveGuideOverlay.jsx'
import { VideoPlayer } from './components/VideoPlayer.jsx'
import { useAirMouse } from './hooks/useAirMouse.js'
import { useKeyboardNav } from './hooks/useKeyboardNav.js'
import { useWakeLock } from './hooks/useWakeLock.js'
import { usePlayer } from './store/PlayerContext.jsx'

export default function App() {
  const { isFullscreen, goBack, selectedChannel } = usePlayer()
  useKeyboardNav()
  useAirMouse()
  useWakeLock(Boolean(selectedChannel))

  useEffect(() => {
    if (!Capacitor.isNativePlatform()) return undefined
    StatusBar.setStyle({ style: Style.Dark }).catch(() => {})
    StatusBar.setBackgroundColor({ color: '#06070a' }).catch(() => {})
    let handle
    CapApp.addListener('backButton', () => {
      goBack()
    }).then((listener) => {
      handle = listener
    })
    return () => {
      handle?.remove()
    }
  }, [goBack])

  return (
    <div className="relative flex h-full flex-col bg-void text-white">
      {!isFullscreen ? <TopBar /> : null}

      <div className="flex min-h-0 flex-1">
        {!isFullscreen ? <Sidebar /> : null}
        {!isFullscreen ? <ChannelList /> : null}
        <VideoPlayer fullscreen={isFullscreen} />
      </div>

      {!isFullscreen ? <EpgTimeline /> : null}
      <PlaylistModal />
      <UpdateDialog />
      <MainMenu />
      <SettingsScreen />
      <SearchOverlay />
      <LiveGuideOverlay />
      <RecordingsScreen />
      <MultiViewScreen />
    </div>
  )
}
