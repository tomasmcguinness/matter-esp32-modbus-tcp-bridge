import { useEffect, useState } from 'react'

type Device = {
  id: string
  name: string
  endpointId: number
}

type EpmReadings = {
  voltage: number | null
  activeCurrent: number | null
  activePower: number | null
}

type PvReadings = {
  voltage: number | null
  activeCurrent: number | null
}

type Readings = {
  electricalPowerMeasurement: EpmReadings
  pv1: PvReadings
  pv2: PvReadings
}

function fmtVoltage(mv: number | null): string {
  if (mv === null) return '—'
  return `${(mv / 1000).toFixed(1)} V`
}

function fmtCurrent(ma: number | null): string {
  if (ma === null) return '—'
  return `${(ma / 1000).toFixed(2)} A`
}

function fmtPower(mw: number | null): string {
  if (mw === null) return '—'
  return `${(mw / 1000).toFixed(0)} W`
}

function PvSensorCard({ label, readings }: { label: string; readings: PvReadings | null }) {
  return (
    <div className="card mt-2">
      <div className="card-header small text-muted">{label}</div>
      <div className="card-body">
        <div className="row g-2">
          <div className="col-6">
            <div className="card text-center h-100">
              <div className="card-body py-2">
                <div className="text-muted small">Voltage</div>
                <div className="fs-5 fw-semibold">{fmtVoltage(readings?.voltage ?? null)}</div>
              </div>
            </div>
          </div>
          <div className="col-6">
            <div className="card text-center h-100">
              <div className="card-body py-2">
                <div className="text-muted small">Current</div>
                <div className="fs-5 fw-semibold">{fmtCurrent(readings?.activeCurrent ?? null)}</div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}

function DeviceCard({ device }: { device: Device }) {
  const [readings, setReadings] = useState<Readings | null>(null)

  useEffect(() => {
    const fetch_readings = () => {
      fetch(`/api/devices/${device.id}/readings`)
        .then((res) => (res.ok ? (res.json() as Promise<Readings>) : Promise.reject()))
        .then(setReadings)
        .catch(() => {})
    }

    fetch_readings()
    const interval = setInterval(fetch_readings, 5000)
    return () => clearInterval(interval)
  }, [device.id])

  const epm = readings?.electricalPowerMeasurement ?? null

  return (
    <div className="card mb-3">
      <div className="card-header d-flex justify-content-between align-items-center">
        <strong>{device.name}</strong>
        {device.endpointId > 0 && (
          <span className="badge bg-secondary">Endpoint {device.endpointId}</span>
        )}
      </div>
      <div className="card-body">
        <div className="card">
          <div className="card-header small text-muted">Electrical Power Measurement</div>
          <div className="card-body">
            <div className="row g-2">
              <div className="col-6 col-md-3">
                <div className="card text-center h-100">
                  <div className="card-body py-2">
                    <div className="text-muted small">Voltage</div>
                    <div className="fs-5 fw-semibold">{fmtVoltage(epm?.voltage ?? null)}</div>
                  </div>
                </div>
              </div>
              <div className="col-6 col-md-3">
                <div className="card text-center h-100">
                  <div className="card-body py-2">
                    <div className="text-muted small">Active Current</div>
                    <div className="fs-5 fw-semibold">{fmtCurrent(epm?.activeCurrent ?? null)}</div>
                  </div>
                </div>
              </div>
              <div className="col-6 col-md-3">
                <div className="card text-center h-100">
                  <div className="card-body py-2">
                    <div className="text-muted small">Active Power</div>
                    <div className="fs-5 fw-semibold">{fmtPower(epm?.activePower ?? null)}</div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>
        <PvSensorCard label="PV String 1" readings={readings?.pv1 ?? null} />
        <PvSensorCard label="PV String 2" readings={readings?.pv2 ?? null} />
      </div>
    </div>
  )
}

function Home() {
  const [devices, setDevices] = useState<Device[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)

  useEffect(() => {
    fetch('/api/devices')
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`)
        return res.json() as Promise<Device[]>
      })
      .then(setDevices)
      .catch((e) => setError(e.message))
      .finally(() => setLoading(false))
  }, [])

  return (
    <>
      <h1>Matter Devices</h1>
      <hr />
      {loading && <p>Loading…</p>}
      {error && <p className="text-danger">Failed: {error}</p>}
      {!loading && !error && devices.length === 0 && (
        <p className="text-muted">No devices configured.</p>
      )}
      {devices.map((d) => (
        <DeviceCard key={d.id} device={d} />
      ))}
    </>
  )
}

export default Home
