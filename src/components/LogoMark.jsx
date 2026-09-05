import { useEffect, useState } from 'react'

function TvPlaceholder() {
  return (
    <svg viewBox="0 0 48 48" className="h-full w-full" aria-hidden="true">
      <rect width="48" height="48" rx="10" fill="#121821" />
      <path d="M17 11l7 5.5L31 11" fill="none" stroke="#3d4d66" strokeWidth="2" strokeLinecap="round" />
      <rect x="10" y="16" width="28" height="20" rx="4" fill="#0b1018" stroke="#2f7cf6" strokeWidth="1.5" />
      <rect x="13" y="19" width="22" height="14" rx="2.5" fill="#172033" />
      <path d="M16 24h6l-4 5" fill="none" stroke="#5aa4ff" strokeWidth="1.4" strokeLinecap="round" />
      <rect x="18" y="38" width="12" height="2.2" rx="1" fill="#2a3548" />
    </svg>
  )
}

export function LogoMark({ name, logo, size = 42 }) {
  const [broken, setBroken] = useState(false)

  useEffect(() => {
    setBroken(false)
  }, [logo])

  const showImage = Boolean(logo) && !broken

  return (
    <div
      className="relative shrink-0 overflow-hidden rounded-lg border border-white/6 bg-[#121821]"
      style={{ width: size, height: size }}
      title={name}
    >
      <TvPlaceholder />
      {showImage ? (
        <img
          src={logo}
          alt=""
          className="absolute inset-0 h-full w-full bg-[#0b1018] object-contain p-0.5"
          onError={() => setBroken(true)}
        />
      ) : null}
    </div>
  )
}
