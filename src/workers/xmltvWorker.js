import { parseXmltvBuffer } from '../lib/xmltv.js'

self.onmessage = async (event) => {
  try {
    const payload = await parseXmltvBuffer(event.data.buffer)
    self.postMessage({ ok: true, payload })
  } catch (error) {
    self.postMessage({ ok: false, error: error.message || String(error) })
  }
}
