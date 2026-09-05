import { useEffect } from 'react'
import {
  arrowDir,
  channelDelta,
  isBackKey,
  isGuideKey,
  isMenuKey,
  isMuteKey,
  isOkKey,
  isSearchKey,
  isTypingTarget,
  matchesBinding,
  volumeDelta,
} from '../lib/remoteKeys.js'
import { usePlayer } from '../store/PlayerContext.jsx'

const ZONES = ['groups', 'channels', 'player']

export function useKeyboardNav() {
  const {
    focusZone,
    setFocusZone,
    isFullscreen,
    setIsFullscreen,
    isModalOpen,
    uiScreen,
    openMenu,
    closeOverlays,
    setUiScreen,
    moveChannel,
    moveGroup,
    selectedChannel,
    toggleFavorite,
    nudgeVolume,
    toggleMute,
    requestRecord,
    requestPip,
    requestVoiceSearch,
    toggleLiveGuide,
    liveGuideOpen,
    goBack,
    settings,
  } = usePlayer()

  useEffect(() => {
    const onKeyDown = (event) => {
      const keys = settings.keys || {}
      const typing = isTypingTarget(event.target)

      if (isBackKey(event) && !typing) {
        event.preventDefault()
        goBack()
        return
      }

      if (uiScreen === 'menu' || uiScreen === 'settings' || uiScreen === 'recordings' || uiScreen === 'multiview') {
        return
      }

      if (uiScreen === 'search') {
        if (isSearchKey(event) && !typing) {
          event.preventDefault()
          requestVoiceSearch()
        }
        return
      }

      if (isModalOpen) return

      if (liveGuideOpen) return

      if ((isMenuKey(event) || matchesBinding(event, keys.menu)) && !typing) {
        event.preventDefault()
        openMenu()
        return
      }

      if ((isSearchKey(event) || matchesBinding(event, keys.voice) || matchesBinding(event, keys.search)) && !typing) {
        event.preventDefault()
        if (isSearchKey(event) || matchesBinding(event, keys.voice)) requestVoiceSearch()
        else setUiScreen('search')
        return
      }

      if ((isGuideKey(event) || matchesBinding(event, keys.guide)) && !typing) {
        event.preventDefault()
        if (selectedChannel) toggleLiveGuide()
        return
      }

      if (matchesBinding(event, keys.record) && !typing) {
        event.preventDefault()
        requestRecord()
        return
      }

      if (matchesBinding(event, keys.pip) && !typing) {
        event.preventDefault()
        requestPip()
        return
      }

      const vol = volumeDelta(event)
      if (vol) {
        event.preventDefault()
        nudgeVolume(vol)
        return
      }

      if (isMuteKey(event) || matchesBinding(event, keys.mute)) {
        if (typing) return
        event.preventDefault()
        toggleMute()
        return
      }

      const zap = channelDelta(event)
      if (zap) {
        event.preventDefault()
        moveChannel(zap)
        return
      }

      const insidePlayer = focusZone === 'player' || isFullscreen
      const dir = arrowDir(event)

      if (selectedChannel && insidePlayer && !liveGuideOpen && (matchesBinding(event, keys.liveGuide) || dir === 'left')) {
        if (matchesBinding(event, keys.liveGuide) || keys.liveGuide === 'ArrowLeft' || !keys.liveGuide) {
          event.preventDefault()
          toggleLiveGuide()
          return
        }
      }

      if (isOkKey(event) || matchesBinding(event, keys.fullscreen)) {
        if (typing) return
        event.preventDefault()
        if (!selectedChannel) return
        if (isFullscreen) {
          window.dispatchEvent(new Event('mirefir:pad'))
          return
        }
        setIsFullscreen(true)
        return
      }

      if (matchesBinding(event, keys.favorite) && !typing) {
        event.preventDefault()
        if (selectedChannel) toggleFavorite(selectedChannel.id)
        return
      }

      if (!dir) return

      event.preventDefault()

      if (insidePlayer && (dir === 'left' || dir === 'right')) {
        nudgeVolume(dir === 'right' ? 1 : -1)
        return
      }

      if (dir === 'up') {
        if (!isFullscreen && focusZone === 'groups') moveGroup(-1)
        else moveChannel(-1)
        return
      }

      if (dir === 'down') {
        if (!isFullscreen && focusZone === 'groups') moveGroup(1)
        else moveChannel(1)
        return
      }

      const step = dir === 'right' ? 1 : -1
      const index = Math.max(0, ZONES.indexOf(focusZone))
      setFocusZone(ZONES[Math.min(ZONES.length - 1, Math.max(0, index + step))])
    }

    window.addEventListener('keydown', onKeyDown)
    return () => window.removeEventListener('keydown', onKeyDown)
  }, [
    closeOverlays,
    focusZone,
    goBack,
    isFullscreen,
    isModalOpen,
    liveGuideOpen,
    moveChannel,
    moveGroup,
    nudgeVolume,
    openMenu,
    requestPip,
    requestRecord,
    requestVoiceSearch,
    selectedChannel,
    setFocusZone,
    setIsFullscreen,
    setUiScreen,
    settings.keys,
    toggleFavorite,
    toggleLiveGuide,
    toggleMute,
    uiScreen,
  ])
}
