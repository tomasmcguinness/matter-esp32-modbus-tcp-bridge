import { useEffect, useRef, useState } from 'react'
import QRCode from 'react-qr-code'

const WINDOW_SECONDS = 180

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

function fmtManualCode(code: string): string {
  if (code.length === 11) return `${code.slice(0, 4)}-${code.slice(4, 7)}-${code.slice(7)}`
  if (code.length === 21) return `${code.slice(0, 6)}-${code.slice(6, 11)}-${code.slice(11, 16)}-${code.slice(16)}`
  return code
}

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
        if (data.windowSecondsRemaining > 0) {
          startCountdown(data.windowSecondsRemaining)
        } else {
          if (timerRef.current) { clearInterval(timerRef.current); timerRef.current = null }
          setCountdown(null)
        }
      })
      .catch((e) => setError(e.message))
  }

  useEffect(() => {
    fetchPairingInfo()
  }, [])

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
        } catch { /* ignore */ }
      }
      ws.onclose = () => { reconnectTimer = setTimeout(connect, 3000) }
      ws.onerror = () => ws.close()
    }

    connect()
    return () => { clearTimeout(reconnectTimer); ws?.close() }
  }, [])

  function openWindow() {
    setOpening(true)
    fetch('/api/matter/open-commissioning-window', { method: 'POST' })
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`)
        startCountdown(WINDOW_SECONDS)
      })
      .catch((e) => setError(`Failed to open window: ${e.message}`))
      .finally(() => setOpening(false))
  }

  useEffect(() => () => { if (timerRef.current) clearInterval(timerRef.current) }, [])

  return (
    <>
      <h1>Matter</h1>
      <hr />

      {error && <p className="text-danger">{error}</p>}
      {!info && !error && <p>Loading…</p>}

      {info && countdown !== null && (
        <>
          <div className="d-flex align-items-center gap-3 mb-3">
            <span className="badge bg-primary fs-6">Window open — {countdown}s remaining</span>
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

      {info && countdown === null && (
        <>
          {info.fabrics.length === 0 ? (
            <p className="text-muted">Not commissioned to any fabric.</p>
          ) : (
            <table className="table table-bordered mb-3">
              <thead>
                <tr>
                  <th>#</th>
                  <th>Label</th>
                  <th>Vendor ID</th>
                  <th>Node ID</th>
                </tr>
              </thead>
              <tbody>
                {info.fabrics.map((f) => (
                  <tr key={f.fabricIndex}>
                    <td>{f.fabricIndex}</td>
                    <td>{f.label || <span className="text-muted fst-italic">unlabelled</span>}</td>
                    <td>{`0x${f.vendorId.toString(16).toUpperCase().padStart(4, '0')}`}</td>
                    <td><code>{f.nodeId}</code></td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
          <button className="btn btn-outline-primary btn-sm" onClick={openWindow} disabled={opening}>
            {opening ? 'Opening…' : 'Open Commissioning Window'}
          </button>
        </>
      )}
    </>
  )
}

export default Commissioning
