import { App as CapApp } from '@capacitor/app'
import { Capacitor } from '@capacitor/core'

export function quitApp() {
  if (Capacitor.isNativePlatform()) {
    CapApp.exitApp().catch(() => {})
    return
  }
  if (window.mirefir?.quit) {
    window.mirefir.quit()
    return
  }
  window.close()
}
