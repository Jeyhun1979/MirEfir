import { useEffect, useRef } from 'react'
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
  seekDelta,
  volumeDelta,
} from '../lib/remoteKeys.js'
import { usePlayer } from '../store/PlayerContext.jsx'

const ZONES = ['groups', 'channels', 'player']

function wrapIndex(index, length) {
  if (!length) return 0
  return (index + length) % length
}

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
    channels,
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
    selectedGroupId,
    channelMenu,
    setChannelMenu,
    openChannelMenu,
    channelMenuItems,
    runChannelMenuItem,
    movingFavoriteId,
    moveFavorite,
    commitFavoriteMove,
  } = usePlayer()

  const okTimer = useRef(0)
  const okHeld = useRef(false)

  useEffect(() => {
    const clearOkHold = () => window.clearTimeout(okTimer.current)

    const startOkHold = () => {
      if (liveGuideOpen || !selectedChannel) return
      okHeld.current = false
      clearOkHold()
      okTimer.current = window.setTimeout(() => {
        okHeld.current = true
        openChannelMenu(selectedChannel, 0)
      }, 550)
    }

    const shortOk = () => {
      if (!selectedChannel) return
      if (isFullscreen) {
        window.dispatchEvent(new Event('mirefir:pad'))
        return
      }
      setIsFullscreen(true)
    }

    const onKeyUp = (event) => {
      if (liveGuideOpen || isTypingTarget(event.target)) return
      if (!isOkKey(event) && !matchesBinding(event, settings.keys?.fullscreen)) return
      clearOkHold()
      if (okHeld.current) return
      event.preventDefault()
      if (movingFavoriteId) {
        commitFavoriteMove()
        return
      }
      if (channelMenu) {
        const channel = channels.find((item) => item.id === channelMenu.channelId) || selectedChannel
        const items = channelMenuItems(channel)
        const item = items[channelMenu.cursor] || items[0]
        runChannelMenuItem(channel, item?.id)
        return
      }
      if (uiScreen || isModalOpen) return
      shortOk()
    }

    const onKeyDown = (event) => {
      const keys = settings.keys || {}
      const typing = isTypingTarget(event.target)

      if (isBackKey(event) && !typing) {
        event.preventDefault()
        goBack()
        return
      }

      if (uiScreen === 'menu' || uiScreen === 'settings' || uiScreen === 'recordings' || uiScreen === 'multiview' || uiScreen === 'history') {
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

      if (movingFavoriteId && !typing) {
        const dir = arrowDir(event)
        if (dir === 'up' || dir === 'down') {
          event.preventDefault()
          moveFavorite(movingFavoriteId, dir === 'down' ? 1 : -1)
          return
        }
        if (isOkKey(event)) {
          event.preventDefault()
          return
        }
        if (dir || isMenuKey(event) || matchesBinding(event, keys.menu)) {
          event.preventDefault()
          return
        }
      }

      if (channelMenu && !liveGuideOpen && !typing) {
        const dir = arrowDir(event)
        const channel = channels.find((item) => item.id === channelMenu.channelId) || selectedChannel
        const items = channelMenuItems(channel)
        if (dir === 'up' || dir === 'down') {
          event.preventDefault()
          setChannelMenu((current) =>
            current
              ? { ...current, cursor: wrapIndex((current.cursor || 0) + (dir === 'down' ? 1 : -1), items.length || 1) }
              : current,
          )
          return
        }
        if (isOkKey(event)) {
          event.preventDefault()
          return
        }
        if (isMenuKey(event) || matchesBinding(event, keys.menu)) {
          event.preventDefault()
          setChannelMenu(null)
          return
        }
      }

      const vol = volumeDelta(event)
      if (vol && !typing) {
        event.preventDefault()
        nudgeVolume(vol)
        return
      }

      if (liveGuideOpen) return

      const seek = seekDelta(event)
      if (seek && selectedChannel && !typing) {
        event.preventDefault()
        window.dispatchEvent(new CustomEvent('mirefir:seek', { detail: { seconds: seek, hold: event.repeat } }))
        return
      }

      if ((isMenuKey(event) || matchesBinding(event, keys.menu)) && !typing) {
        event.preventDefault()
        if (selectedChannel && selectedGroupId === 'favorites') {
          openChannelMenu(selectedChannel, 0)
          return
        }
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

      if (isFullscreen && !liveGuideOpen && selectedChannel && dir === 'down' && !typing) {
        event.preventDefault()
        event.stopPropagation()
        window.dispatchEvent(new Event('mirefir:pad-down'))
        return
      }

      if (selectedChannel && insidePlayer && !liveGuideOpen && (dir === 'left' || matchesBinding(event, keys.liveGuide))) {
        event.preventDefault()
        toggleLiveGuide()
        return
      }

      if (isOkKey(event) || matchesBinding(event, keys.fullscreen)) {
        if (typing) return
        event.preventDefault()
        if (!selectedChannel || event.repeat) return
        startOkHold()
        return
      }

      if (matchesBinding(event, keys.favorite) && !typing) {
        event.preventDefault()
        if (selectedChannel) toggleFavorite(selectedChannel.id)
        return
      }

      if (!dir) return

      event.preventDefault()

      if (insidePlayer && (dir === 'left' || dir === 'right')) return

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
    window.addEventListener('keyup', onKeyUp)
    return () => {
      window.removeEventListener('keydown', onKeyDown)
      window.removeEventListener('keyup', onKeyUp)
      clearOkHold()
    }
  }, [
    channelMenu,
    channelMenuItems,
    channels,
    closeOverlays,
    commitFavoriteMove,
    focusZone,
    goBack,
    isFullscreen,
    isModalOpen,
    liveGuideOpen,
    moveChannel,
    moveFavorite,
    moveGroup,
    movingFavoriteId,
    nudgeVolume,
    openChannelMenu,
    openMenu,
    requestPip,
    requestRecord,
    requestVoiceSearch,
    runChannelMenuItem,
    selectedChannel,
    selectedGroupId,
    setChannelMenu,
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
