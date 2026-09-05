import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'
import tailwindcss from '@tailwindcss/vite'

async function proxyRemote(req, res, asText) {
  const target = new URL(req.url, 'http://127.0.0.1').searchParams.get('url')
  if (!target) {
    res.statusCode = 400
    res.end('Missing url')
    return
  }

  try {
    const remote = await fetch(target, {
      redirect: 'follow',
      headers: {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) MirEfir/1.0',
        Accept: '*/*',
      },
    })
    const buffer = Buffer.from(await remote.arrayBuffer())
    res.statusCode = remote.ok ? 200 : remote.status
    res.setHeader('Access-Control-Allow-Origin', '*')
    if (asText) {
      res.setHeader('Content-Type', 'text/plain; charset=utf-8')
      res.end(buffer.toString('utf8'))
      return
    }
    res.setHeader('Content-Type', 'application/octet-stream')
    res.end(buffer)
  } catch (error) {
    res.statusCode = 502
    res.end(error.message || 'Proxy error')
  }
}

function playlistProxy() {
  return {
    name: 'playlist-proxy',
    configureServer(server) {
      server.middlewares.use('/api/playlist', (req, res) => proxyRemote(req, res, true))
      server.middlewares.use('/api/fetch', (req, res) => proxyRemote(req, res, false))
    },
  }
}

export default defineConfig({
  plugins: [react(), tailwindcss(), playlistProxy()],
  base: './',
  server: {
    host: '127.0.0.1',
    port: 5173,
    strictPort: true,
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true,
  },
})
