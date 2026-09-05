import { useEffect, useState } from 'react'

export function useAirMouse() {
  const [visible, setVisible] = useState(false)

  useEffect(() => {
    let hide = 0
    const onMove = () => {
      document.documentElement.classList.add('air-mouse')
      setVisible(true)
      window.clearTimeout(hide)
      hide = window.setTimeout(() => setVisible(false), 4000)
    }
    window.addEventListener('pointermove', onMove, { passive: true })
    return () => {
      window.removeEventListener('pointermove', onMove)
      window.clearTimeout(hide)
    }
  }, [])

  return visible
}
