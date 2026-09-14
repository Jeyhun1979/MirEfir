import { usePlayer } from '../store/PlayerContext.jsx'

export function BootOverlay() {
  const { bootScreen } = usePlayer()
  if (!bootScreen) return null

  const install = bootScreen === 'install'
  return (
    <div className="absolute inset-0 z-[85] flex flex-col bg-[#06070a] text-white">
      <div className="flex min-h-0 flex-1 flex-col items-center justify-center px-6">
        <div className="flex h-12 w-12 items-center justify-center rounded-xl bg-accent text-lg font-bold">▶</div>
        <h1 className="mt-5 text-[28px] font-semibold tracking-tight">{install ? 'Установка' : 'Загрузка'}</h1>
        <p className="mt-2 text-sm text-white/55">
          {install ? 'Обновление установлено, запускаем приложение' : 'Открываем эфир и телепрограмму'}
        </p>
        <div className="boot-bar mt-7 h-2 w-[min(320px,70vw)] overflow-hidden rounded-full bg-white/10">
          <i />
        </div>
      </div>
    </div>
  )
}
