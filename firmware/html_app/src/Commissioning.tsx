import { useEffect, useRef, useState } from 'react'
import QRCode from 'react-qr-code'

const WINDOW_SECONDS = 180

type PairingInfo = {
  commissioned: boolean
  qrCode: string
  manualPairingCode: string
}

function fmtManualCode(code: string): string {
  if (code.length === 11) return `${code.slice(0, 5)}-${code.slice(5)}`
  if (code.length === 21) return `${code.slice(0, 5)}-${code.slice(5, 10)}-${code.slice(10, 15)}-${code.slice(15)}`
  return code
}

function Commissioning() {
  const [info, setInfo] = useState<PairingInfo | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [opening, setOpening] = useState(false)
  const [countdown, setCountdown] = useState<number | null>(null)
  const timerRef = useRef<ReturnType<typeof setInterval> | null>(null)

  useEffect(() => {
    fetch('/api/matter/pairing')
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`)
        return res.json() as Promise<PairingInfo>
      })
      .then(setInfo)
      .catch((e) => setError(e.message))
  }, [])

  function openWindow() {
    setOpening(true)
    fetch('/api/matter/open-commissioning-window', { method: 'POST' })
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`)
        setCountdown(WINDOW_SECONDS)
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
      })
      .catch((e) => setError(`Failed to open window: ${e.message}`))
      .finally(() => setOpening(false))
  }

  useEffect(() => () => { if (timerRef.current) clearInterval(timerRef.current) }, [])

  return (
    <>
      <h1>Matter Commissioning</h1>
      <hr />

      {error && <p className="text-danger">{error}</p>}
      {!info && !error && <p>Loading…</p>}

      {info && (
        <>
          <div className="d-flex align-items-center gap-3 mb-3">
            {info.commissioned ? (
              <span className="badge bg-success fs-6">Commissioned</span>
            ) : (
              <span className="badge bg-warning text-dark fs-6">Not Commissioned</span>
            )}
            {countdown !== null ? (
              <span className="badge bg-primary fs-6">
                Window open — {countdown}s remaining
              </span>
            ) : (
              <button className="btn btn-outline-primary btn-sm" onClick={openWindow} disabled={opening}>
                {opening ? 'Opening…' : 'Open Commissioning Window'}
              </button>
            )}
          </div>

          <div className="row g-4">
            <div className="col-md-auto">
              <div className="card">
                <div className="card-header small text-muted">QR Code</div>
                <div className="card-body d-flex justify-content-center p-3">
                  <QRCode value={info.qrCode} size={200} />
                </div>
              </div>
            </div>

            <div className="col">
              <div className="card mb-3">
                <div className="card-header small text-muted">Manual Pairing Code</div>
                <div className="card-body">
                  <span className="font-monospace fs-3 fw-semibold">
                    {fmtManualCode(info.manualPairingCode)}
                  </span>
                </div>
              </div>

              <div className="card">
                <div className="card-header small text-muted">Setup Payload</div>
                <div className="card-body">
                  <code className="text-break">{info.qrCode}</code>
                </div>
              </div>
            </div>
          </div>
        </>
      )}
    </>
  )
}

export default Commissioning
