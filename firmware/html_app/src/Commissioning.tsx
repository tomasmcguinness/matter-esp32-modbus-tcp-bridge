import { useEffect, useRef, useState } from 'react'
import QRCode from 'react-qr-code'

const WINDOW_SECONDS = 180

// ── Types ──────────────────────────────────────────────────────────────────

type Fabric = {
  fabricIndex: number
  vendorId: number
  label: string
  nodeId: string
}

type PairingInfo = {
  qrCode: string
  manualPairingCode: string
  windowSecondsRemaining: number
  fabrics: Fabric[]
}

// ── Helpers ────────────────────────────────────────────────────────────────

// Friendly names for common Matter ecosystem vendor IDs (presentation only —
// falls back to the hex code for anything unrecognised).
const VENDOR_NAMES: Record<number, string> = {
  0x1349: 'Apple',
  0x6006: 'Google',
  0x1049: 'Samsung SmartThings',
  0x1217: 'Amazon',
  0x100b: 'Home Assistant',
}

function vendorHex(vendorId: number): string {
  return `0x${vendorId.toString(16).toUpperCase().padStart(4, '0')}`
}

function vendorLabel(f: Fabric): string {
  if (f.label && f.label.trim()) return f.label.trim()
  return VENDOR_NAMES[f.vendorId] ?? 'Matter controller'
}

function fmtManualCode(code: string): string {
  if (code.length === 11) return `${code.slice(0, 4)}-${code.slice(4, 7)}-${code.slice(7)}`
  if (code.length === 21)
    return `${code.slice(0, 6)}-${code.slice(6, 11)}-${code.slice(11, 16)}-${code.slice(16)}`
  return code
}

function fmtCountdown(seconds: number): string {
  const m = Math.floor(seconds / 60)
  const s = String(seconds % 60).padStart(2, '0')
  return `${m}:${s}`
}

// ── Component ──────────────────────────────────────────────────────────────

function Commissioning() {
  const [info, setInfo] = useState<PairingInfo | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [opening, setOpening] = useState(false)
  const [countdown, setCountdown] = useState<number | null>(null)
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null)

  function startCountdown(seconds: number) {
    if (timerRef.current) clearInterval(timerRef.current)
    setCountdown(seconds)
    timerRef.current = setInterval(() => {
      setCountdown((c) => {
        if (c === null || c <= 1) {
          clearInterval(timerRef.current!)
          timerRef.current = null
          return null
        }
        return c - 1
      })
    }, 1000)
  }

  function fetchPairingInfo() {
    fetch('/api/matter/pairing')
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`)
        return res.json() as Promise<PairingInfo>
      })
      .then((data) => {
        setInfo(data)
        setError(null)
        if (data.windowSecondsRemaining > 0) {
          startCountdown(data.windowSecondsRemaining)
        } else {
          if (timerRef.current) {
            clearInterval(timerRef.current)
            timerRef.current = null
          }
          setCountdown(null)
        }
      })
      .catch((e) => setError(e instanceof Error ? e.message : String(e)))
  }

  useEffect(() => {
    fetchPairingInfo()
  }, [])

  // Live updates: the firmware pushes window_closed / commissioned events over WS.
  useEffect(() => {
    let ws: WebSocket
    let reconnectTimer: ReturnType<typeof setTimeout>

    function connect() {
      ws = new WebSocket(`ws://${location.host}/api/matter/events`)
      ws.onmessage = (e) => {
        try {
          const msg = JSON.parse(e.data as string) as { type: string }
          if (msg.type === 'window_closed' || msg.type === 'commissioned') {
            fetchPairingInfo()
          }
        } catch {
          /* ignore */
        }
      }
      ws.onclose = () => {
        reconnectTimer = setTimeout(connect, 3000)
      }
      ws.onerror = () => ws.close()
    }

    connect()
    return () => {
      clearTimeout(reconnectTimer)
      ws?.close()
    }
  }, [])

  function openWindow() {
    setOpening(true)
    fetch('/api/matter/open-commissioning-window', { method: 'POST' })
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`)
        startCountdown(WINDOW_SECONDS)
      })
      .catch((e) => setError(`Failed to open window: ${e instanceof Error ? e.message : String(e)}`))
      .finally(() => setOpening(false))
  }

  useEffect(
    () => () => {
      if (timerRef.current) clearInterval(timerRef.current)
    },
    [],
  )

  const windowOpen = info !== null && countdown !== null
  const fabricCount = info?.fabrics.length ?? 0

  const subtitle = !info
    ? 'Loading…'
    : windowOpen
      ? 'Window open — share either code with your controller'
      : `${fabricCount} fabric${fabricCount === 1 ? '' : 's'} commissioned · window closed`

  // ── Render ─────────────────────────────────────────────────────────────────

  return (
    <div style={{ paddingTop: 28, paddingBottom: 64 }}>
      {/* Header row */}
      <div
        style={{
          display: 'flex',
          alignItems: 'flex-end',
          justifyContent: 'space-between',
          flexWrap: 'wrap',
          gap: 16,
          marginBottom: 22,
        }}
      >
        <div>
          <h1 style={{ margin: '0 0 5px', fontSize: 26, fontWeight: 700, letterSpacing: '-.02em' }}>
            Matter Commissioning
          </h1>
          <p style={{ margin: 0, color: '#6c757d', fontSize: 14 }}>{subtitle}</p>
        </div>
        <span
          style={{
            display: 'inline-flex',
            alignItems: 'center',
            gap: 7,
            fontSize: 12.5,
            fontWeight: 600,
            color: windowOpen ? '#0d6efd' : '#2e9e6b',
            fontFamily: 'monospace',
          }}
        >
          <span
            style={{
              width: 7,
              height: 7,
              borderRadius: '50%',
              background: windowOpen ? '#0d6efd' : '#2e9e6b',
              animation: windowOpen ? 'commPulse 1.6s ease-in-out infinite' : undefined,
            }}
          />
          {windowOpen ? 'window open' : 'adapter online'}
        </span>
      </div>

      <style>{`@keyframes commPulse{0%,100%{opacity:1}50%{opacity:.35}}`}</style>

      {/* Error banner */}
      {error && (
        <div
          style={{
            background: '#fff5f5',
            border: '1px solid #ffc9c9',
            borderRadius: 8,
            padding: '10px 16px',
            color: '#c92a2a',
            fontSize: 14,
            marginBottom: 18,
          }}
        >
          {error}
        </div>
      )}

      {/* Loading skeleton */}
      {!info && !error && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 12 }}>
          {[1, 2, 3].map((n) => (
            <div
              key={n}
              style={{
                height: n === 1 ? 56 : 84,
                background: '#f8f9fa',
                borderRadius: 10,
                border: '1px solid #e9ecef',
                opacity: 1 - n * 0.18,
              }}
            />
          ))}
        </div>
      )}

      {/* ── Window OPEN ──────────────────────────────────────────────────── */}
      {windowOpen && info && (
        <>
          {/* Countdown banner */}
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: 12,
              flexWrap: 'wrap',
              background: '#e7f1ff',
              border: '1px solid #cfe2ff',
              borderRadius: 10,
              padding: '12px 18px',
              marginBottom: 18,
            }}
          >
            <span
              style={{
                width: 8,
                height: 8,
                borderRadius: '50%',
                background: '#0d6efd',
                animation: 'commPulse 1.4s ease-in-out infinite',
              }}
            />
            <span style={{ fontSize: 14, fontWeight: 600, color: '#0d6efd' }}>
              Commissioning window open
            </span>
            <span
              style={{
                marginLeft: 'auto',
                fontFamily: 'monospace',
                fontSize: 13,
                fontWeight: 600,
                color: '#0d6efd',
              }}
            >
              {fmtCountdown(countdown!)} remaining
            </span>
          </div>

          <div style={{ display: 'flex', gap: 18, flexWrap: 'wrap', alignItems: 'stretch' }}>
            {/* QR card */}
            <div
              style={{
                flex: '0 0 auto',
                width: 258,
                background: '#fff',
                border: '1px solid #e9ecef',
                borderRadius: 12,
                overflow: 'hidden',
                display: 'flex',
                flexDirection: 'column',
              }}
            >
              <div style={cardHeaderStyle}>QR Code</div>
              <div
                style={{
                  flex: '1 1 auto',
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  padding: 24,
                }}
              >
                <div style={{ background: '#fff', padding: 6 }}>
                  <QRCode value={info.qrCode} size={190} />
                </div>
              </div>
            </div>

            {/* Right column */}
            <div
              style={{
                flex: '1 1 320px',
                minWidth: 280,
                display: 'flex',
                flexDirection: 'column',
                gap: 14,
              }}
            >
              {/* Manual pairing code */}
              <div style={cardStyle}>
                <div style={cardHeaderStyle}>Manual Pairing Code</div>
                <div style={{ padding: '18px 20px 20px' }}>
                  <div
                    style={{
                      fontFamily: 'monospace',
                      fontSize: 30,
                      fontWeight: 600,
                      letterSpacing: '.03em',
                      color: '#212529',
                    }}
                  >
                    {fmtManualCode(info.manualPairingCode)}
                  </div>
                  <div style={{ marginTop: 8, fontSize: 13, color: '#6c757d' }}>
                    Enter this code in any Matter controller — Apple Home, Google Home, SmartThings
                    or Alexa.
                  </div>
                </div>
              </div>

              {/* Setup payload */}
              <div style={cardStyle}>
                <div style={cardHeaderStyle}>Setup Payload</div>
                <div style={{ padding: '14px 20px' }}>
                  <code
                    style={{
                      display: 'block',
                      fontFamily: 'monospace',
                      fontSize: 13,
                      color: '#495057',
                      wordBreak: 'break-all',
                      lineHeight: 1.5,
                    }}
                  >
                    {info.qrCode}
                  </code>
                </div>
              </div>
            </div>
          </div>
        </>
      )}

      {/* ── Window CLOSED ────────────────────────────────────────────────── */}
      {info && !windowOpen && (
        <>
          {/* Commissioned fabrics */}
          {info.fabrics.length > 0 ? (
            <div style={{ ...cardStyle, marginBottom: 18 }}>
              <div
                style={{
                  display: 'flex',
                  alignItems: 'center',
                  gap: 10,
                  padding: '14px 20px',
                  borderBottom: '1px solid #f1f3f5',
                }}
              >
                <span style={{ fontSize: 13, fontWeight: 600, color: '#343a40' }}>
                  Commissioned Fabrics
                </span>
                <span style={{ fontFamily: 'monospace', fontSize: 12, color: '#adb5bd' }}>
                  {fabricCount}
                </span>
                <span
                  style={{
                    marginLeft: 'auto',
                    display: 'inline-flex',
                    alignItems: 'center',
                    gap: 6,
                    padding: '4px 11px',
                    borderRadius: 20,
                    background: '#e9f7ef',
                  }}
                >
                  <span
                    style={{ width: 6, height: 6, borderRadius: '50%', background: '#2e9e6b' }}
                  />
                  <span style={{ fontSize: 11.5, fontWeight: 600, color: '#2e9e6b' }}>Paired</span>
                </span>
              </div>

              {info.fabrics.map((f) => (
                <div
                  key={f.fabricIndex}
                  style={{
                    display: 'flex',
                    alignItems: 'center',
                    gap: 16,
                    padding: '14px 20px',
                    borderBottom: '1px solid #f1f3f5',
                  }}
                >
                  <div
                    style={{
                      flex: 'none',
                      width: 30,
                      height: 30,
                      borderRadius: 8,
                      background: '#f1f3f5',
                      border: '1px solid #e9ecef',
                      display: 'flex',
                      alignItems: 'center',
                      justifyContent: 'center',
                      fontFamily: 'monospace',
                      fontSize: 13,
                      fontWeight: 600,
                      color: '#495057',
                    }}
                  >
                    {f.fabricIndex}
                  </div>
                  <div style={{ flex: '1 1 auto', minWidth: 0 }}>
                    <div style={{ fontSize: 15, fontWeight: 600, letterSpacing: '-.01em' }}>
                      {vendorLabel(f)}
                    </div>
                    <div
                      style={{
                        marginTop: 3,
                        fontFamily: 'monospace',
                        fontSize: 12,
                        color: '#adb5bd',
                      }}
                    >
                      vendor {vendorHex(f.vendorId)}
                    </div>
                  </div>
                  <div style={{ flex: 'none', textAlign: 'right' }}>
                    <div
                      style={{
                        fontSize: 10,
                        fontWeight: 600,
                        letterSpacing: '.07em',
                        textTransform: 'uppercase',
                        color: '#adb5bd',
                        marginBottom: 2,
                      }}
                    >
                      Node ID
                    </div>
                    <code style={{ fontFamily: 'monospace', fontSize: 12.5, color: '#343a40' }}>
                      {f.nodeId}
                    </code>
                  </div>
                </div>
              ))}
            </div>
          ) : (
            <div
              style={{
                border: '1.5px dashed #dee2e6',
                borderRadius: 12,
                background: '#f8f9fa',
                padding: '44px 24px',
                textAlign: 'center',
                marginBottom: 18,
              }}
            >
              <div style={{ fontSize: 16, fontWeight: 600, marginBottom: 5 }}>
                Not commissioned to any fabric
              </div>
              <div style={{ fontSize: 14, color: '#6c757d' }}>
                Open a commissioning window, then pair the adapter from your Matter controller.
              </div>
            </div>
          )}

          {/* Open-window CTA */}
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: 18,
              flexWrap: 'wrap',
              ...cardStyle,
              padding: '20px 22px',
            }}
          >
            <div style={{ flex: '1 1 300px', minWidth: 240 }}>
              <div style={{ fontSize: 15, fontWeight: 600, marginBottom: 4 }}>
                Add this adapter to a Matter fabric
              </div>
              <div style={{ fontSize: 14, color: '#6c757d' }}>
                Open a 3-minute window to reveal the QR and manual pairing codes.
              </div>
            </div>
            <button
              type="button"
              onClick={openWindow}
              disabled={opening}
              style={{
                height: 44,
                border: 'none',
                background: opening ? '#6ea8fe' : '#0d6efd',
                color: '#fff',
                fontWeight: 600,
                fontSize: 14,
                fontFamily: 'inherit',
                borderRadius: 8,
                padding: '0 20px',
                cursor: opening ? 'default' : 'pointer',
                whiteSpace: 'nowrap',
                boxShadow: '0 1px 2px rgba(13,110,253,.3)',
              }}
            >
              {opening ? 'Opening…' : 'Open Commissioning Window'}
            </button>
          </div>
        </>
      )}
    </div>
  )
}

// ── Shared inline style fragments ──────────────────────────────────────────

const cardStyle: React.CSSProperties = {
  background: '#fff',
  border: '1px solid #e9ecef',
  borderRadius: 12,
  overflow: 'hidden',
}

const cardHeaderStyle: React.CSSProperties = {
  padding: '11px 20px',
  borderBottom: '1px solid #f1f3f5',
  fontSize: 10,
  fontWeight: 600,
  letterSpacing: '.09em',
  textTransform: 'uppercase',
  color: '#adb5bd',
}

export default Commissioning
