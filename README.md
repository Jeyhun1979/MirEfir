# OnePlayer

IPTV-плеер для Windows и Android. Плейлист задаёте сами: ссылка или файл M3U. Телепрограмму можно добавить позже.

## Скачать

Готовые сборки — в [Releases](https://github.com/Jeyhun1979/OnePlayer/releases).

- Windows: `OnePlayer-*-windows.exe` (без установки)
- Linux: `OnePlayer-*-linux-x64.tar.gz` — распаковать и запустить `OnePlayer`
- Android: APK позже
- macOS: сборка только с Mac, в этом релизе нет

При запуске новой версии плеер предложит **Обновить** или **Пропустить**.

## Первый запуск

1. Укажите ссылку на M3U или выберите файл.
2. По желанию добавьте XMLTV (`.xml` / `.xml.gz`).
3. Каналы откроются сразу после загрузки плейлиста.

## Для разработчиков

```bash
npm install
npm run electron:dev
```

Сборка Windows:

```bash
npm run electron:build
```
