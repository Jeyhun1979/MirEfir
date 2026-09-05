import { useEffect } from 'react'

export function useWakeLock(active) {
  useEffect(() => {
    if (!active || !navigator.wakeLock) return undefined
    let lock = null
    const grab = () => {
      navigator.wakeLock.request('screen').then((next) => {
        lock = next
      }).catch(() => {})
    }
    grab()
    document.addEventListener('visibilitychange', grab)
    return () => {
      document.removeEventListener('visibilitychange', grab)
      lock?.release?.()
    }
  }, [active])
}
