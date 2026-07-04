import { useEffect, useState } from 'react'

// ── Constants ──────────────────────────────────────────────────────────────

const ATTRIBUTE_META: Record<number, Record<number, { label: string; unit: string; format: (v: number) => string }>> = {
  0x0090: {
    0x0004: { label: 'Voltage',        unit: 'V',  format: (v) => (v / 1000).toFixed(1) },
    0x0005: { label: 'Current',        unit: 'A',  format: (v) => (v / 1000).toFixed(2) },
    0x0008: { label: 'Power',          unit: 'W',  format: (v) => Math.round(v / 1000).toLocaleString() },
  },
  0x002F: {
    0x000C: { label: 'State of Charge', unit: '%', format: (v) => (v / 2).toFixed(0) },
  },
}

const DEVICE_TYPE_NAMES: Record<number, string> = {
  0x0017: 'Solar Power',
  0x0018: 'Battery Storage',
  0x0011: 'Power Source',
  0x0510: 'Electrical Sensor',
}

// ── Types ──────────────────────────────────────────────────────────────────

type Mapping = {
  function: number
  address: number
  cluster: number
  attribute: number
}

type Part = {
  endpointId?: number
  description?: string
  deviceTypes: number[]
  mappings: Mapping[]
}

type MatterEndpoint = {
  endpointId?: number
  description?: string
  deviceTypes: number[]
  mappings: Mapping[]
  parts?: Part[]
}

type Device = {
  id: string
  name: string
  matter_structure?: { endpoints: MatterEndpoint[] }
}

type EndpointReading = {
  endpointId: number
  clusterId: number
  attributeId: number
  value: number
}

// ── Helpers ────────────────────────────────────────────────────────────────

function chipLabel(dt: number): string {
  return DEVICE_TYPE_NAMES[dt] ?? `0x${dt.toString(16).toUpperCase().padStart(4, '0')}`
}

function makeAttrs(
  mappings: Mapping[],
  epId: number | undefined,
  readings: EndpointReading[],
): { label: string; unit: string; value: string }[] {
  const seen = new Set<string>()
  const result: { label: string; unit: string; value: string }[] = []
  for (const m of mappings) {
    const key = `${m.cluster}-${m.attribute}`
    if (seen.has(key)) continue
    seen.add(key)
    const meta = ATTRIBUTE_META[m.cluster]?.[m.attribute]
    if (!meta) continue
    const reading = readings.find(
      (r) => r.endpointId === epId && r.clusterId === m.cluster && r.attributeId === m.attribute,
    )
    result.push({ label: meta.label, unit: meta.unit, value: reading != null ? meta.format(reading.value) : '—' })
  }
  return result
}

// ── Sub-components ─────────────────────────────────────────────────────────

function AttrTile({ label, unit, value, small }: { label: string; unit: string; value: string; small?: boolean }) {
  return (
    <div
      style={{
        flex: '1 1 ' + (small ? '80px' : '100px'),
        minWidth: small ? 76 : 90,
        maxWidth: small ? 160 : 180,
        background: small ? '#fff' : '#f8f8f5',
        border: '1px solid ' + (small ? '#e9e9e4' : '#ebebE6'),
        borderRadius: small ? 8 : 10,
        padding: small ? '8px 12px' : '10px 14px',
      }}
    >
      <div
        style={{
          fontSize: small ? 9.5 : 10,
          fontWeight: 600,
          letterSpacing: '.07em',
          textTransform: 'uppercase',
          color: '#a4a5a8',
          marginBottom: 3,
        }}
      >
        {label}
      </div>
      <div
        style={{
          fontFamily: 'monospace',
          fontSize: small ? 17 : 19,
          fontWeight: 600,
          color: '#1b1c1e',
          lineHeight: 1.1,
        }}
      >
        {value}
        <span style={{ fontSize: small ? 10.5 : 11.5, fontWeight: 500, color: '#9a9b9e', marginLeft: 2 }}>
          {unit}
        </span>
      </div>
    </div>
  )
}

function Chip({ label, small }: { label: string; small?: boolean }) {
  return (
    <span
      style={{
        fontSize: small ? 10.5 : 11,
        fontWeight: 500,
        color: small ? '#76787d' : '#55565a',
        background: small ? '#f0f0ec' : '#f3f3ef',
        border: '1px solid ' + (small ? '#e6e5e0' : '#ebeae5'),
        borderRadius: small ? 4 : 5,
        padding: small ? '2px 7px' : '2px 8px',
        whiteSpace: 'nowrap',
      }}
    >
      {label}
    </span>
  )
}

function PartSection({ part, readings }: { part: Part; readings: EndpointReading[] }) {
  const attrs = makeAttrs(part.mappings, part.endpointId, readings)
  return (
    <div style={{ border: '1px solid #ebebE6', borderRadius: 10, overflow: 'hidden', background: '#fbfbf9' }}>
      {/* Part header */}
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 10,
          padding: '9px 14px',
          borderBottom: '1px solid #efeeea',
          flexWrap: 'wrap',
        }}
      >
        <span
          style={{
            width: 6,
            height: 6,
            borderRadius: '50%',
            border: '1.5px solid #b6b7ba',
            flexShrink: 0,
            display: 'inline-block',
          }}
        />
        <span style={{ fontSize: 12.5, fontWeight: 600, color: '#55565a' }}>
          {part.description ?? `Endpoint ${part.endpointId}`}
        </span>
        {part.deviceTypes.map((dt) => (
          <Chip key={dt} label={chipLabel(dt)} small />
        ))}
        {part.endpointId != null && (
          <span
            style={{
              marginLeft: 'auto',
              fontFamily: 'monospace',
              fontSize: 11,
              color: '#a4a5a8',
              flexShrink: 0,
            }}
          >
            ep {part.endpointId}
          </span>
        )}
      </div>
      {/* Part attributes */}
      {attrs.length > 0 && (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, padding: '10px 14px' }}>
          {attrs.map((a) => (
            <AttrTile key={a.label} label={a.label} unit={a.unit} value={a.value} small />
          ))}
        </div>
      )}
    </div>
  )
}

function EndpointSection({ endpoint, readings }: { endpoint: MatterEndpoint; readings: EndpointReading[] }) {
  const attrs = makeAttrs(endpoint.mappings, endpoint.endpointId, readings)
  return (
    <div style={{ marginTop: 18 }}>
      {/* Endpoint label row */}
      <div
        style={{
          display: 'flex',
          alignItems: 'center',
          gap: 10,
          flexWrap: 'wrap',
          marginBottom: 10,
        }}
      >
        <span style={{ fontSize: 13, fontWeight: 600, color: '#3a3b3e' }}>
          {endpoint.description ?? `Endpoint ${endpoint.endpointId}`}
        </span>
        {endpoint.deviceTypes.map((dt) => (
          <Chip key={dt} label={chipLabel(dt)} />
        ))}
        {endpoint.endpointId != null && (
          <span
            style={{
              marginLeft: 'auto',
              fontFamily: 'monospace',
              fontSize: 11,
              color: '#a4a5a8',
              flexShrink: 0,
            }}
          >
            ep {endpoint.endpointId}
          </span>
        )}
      </div>
      {/* Attribute tiles */}
      {attrs.length > 0 && (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8, marginBottom: endpoint.parts?.length ? 12 : 0 }}>
          {attrs.map((a) => (
            <AttrTile key={a.label} label={a.label} unit={a.unit} value={a.value} />
          ))}
        </div>
      )}
      {/* Parts */}
      {(endpoint.parts ?? []).length > 0 && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          {(endpoint.parts ?? []).map((part, i) => (
            <PartSection key={part.endpointId ?? i} part={part} readings={readings} />
          ))}
        </div>
      )}
    </div>
  )
}

function DeviceCard({ device }: { device: Device }) {
  const [readings, setReadings] = useState<EndpointReading[]>([])
  const [lastUpdated, setLastUpdated] = useState<string | null>(null)

  useEffect(() => {
    const fetchReadings = () => {
      fetch(`/api/devices/${device.id}/readings`)
        .then((res) => (res.ok ? (res.json() as Promise<{ readings: EndpointReading[] }>) : Promise.reject()))
        .then((data) => {
          setReadings(data.readings)
          const now = new Date()
          setLastUpdated(
            `${now.getHours()}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`,
          )
        })
        .catch(() => {})
    }
    fetchReadings()
    const iv = setInterval(fetchReadings, 5000)
    return () => clearInterval(iv)
  }, [device.id])

  const endpoints = device.matter_structure?.endpoints ?? []

  const epCount = endpoints.reduce((n, ep) => n + 1 + (ep.parts?.length ?? 0), 0)
  const regCount = endpoints.reduce(
    (n, ep) => n + ep.mappings.length + (ep.parts ?? []).reduce((m, p) => m + p.mappings.length, 0),
    0,
  )

  return (
    <div
      style={{
        background: '#fff',
        border: '1px solid #e7e6e2',
        borderRadius: 14,
        overflow: 'hidden',
        boxShadow: '0 1px 2px rgba(20,20,22,.04)',
      }}
    >
      {/* Device header */}
      <div
        style={{
          position: 'relative',
          display: 'flex',
          alignItems: 'center',
          gap: 14,
          padding: '16px 20px 16px 24px',
          borderBottom: '1px solid #efeeea',
        }}
      >
        <div
          style={{
            position: 'absolute',
            left: 0,
            top: 0,
            bottom: 0,
            width: 3,
            background: '#0d6efd',
          }}
        />
        <div style={{ flex: '1 1 auto', minWidth: 0 }}>
          <div style={{ fontSize: 17, fontWeight: 700, letterSpacing: '-.01em' }}>{device.name}</div>
          <div
            style={{
              marginTop: 4,
              display: 'flex',
              alignItems: 'center',
              gap: 10,
              fontFamily: 'monospace',
              fontSize: 12,
              color: '#76787d',
            }}
          >
            <span>{epCount} endpoint{epCount === 1 ? '' : 's'}</span>
            <span
              style={{ width: 3, height: 3, borderRadius: '50%', background: '#cdcdc9', flexShrink: 0, display: 'inline-block' }}
            />
            <span>{regCount} register{regCount === 1 ? '' : 's'}</span>
          </div>
        </div>
        <div
          style={{
            display: 'inline-flex',
            alignItems: 'center',
            gap: 6,
            padding: '5px 11px',
            borderRadius: 20,
            background: '#e9f5ef',
            flexShrink: 0,
          }}
        >
          <span
            style={{
              width: 6,
              height: 6,
              borderRadius: '50%',
              background: '#2e9e6b',
              display: 'inline-block',
              animation: 'pulseDot 2s ease-in-out infinite',
            }}
          />
          <span style={{ fontSize: 11.5, fontWeight: 600, color: '#2e9e6b' }}>Live</span>
        </div>
      </div>

      {/* Endpoints */}
      <div style={{ padding: '0 20px 18px 24px' }}>
        {endpoints.length === 0 ? (
          <p style={{ margin: '18px 0 0', fontSize: 14, color: '#9a9b9e' }}>No endpoints configured.</p>
        ) : (
          endpoints.map((ep, i) => (
            <EndpointSection key={ep.endpointId ?? i} endpoint={ep} readings={readings} />
          ))
        )}
      </div>

      {/* Footer */}
      <div
        style={{
          borderTop: '1px solid #efeeea',
          padding: '9px 20px 9px 24px',
          display: 'flex',
          alignItems: 'center',
          gap: 8,
        }}
      >
        <span
          style={{ width: 6, height: 6, borderRadius: '50%', background: '#2e9e6b', display: 'inline-block', flexShrink: 0 }}
        />
        <span style={{ fontSize: 11.5, color: '#a4a5a8', fontFamily: 'monospace' }}>
          {lastUpdated ? `Last updated ${lastUpdated}` : 'Waiting for readings…'}
        </span>
      </div>
    </div>
  )
}

// ── Home ───────────────────────────────────────────────────────────────────

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
      .catch((e: unknown) => setError(e instanceof Error ? e.message : String(e)))
      .finally(() => setLoading(false))
  }, [])

  const totalEps = devices.reduce(
    (n, d) =>
      n + (d.matter_structure?.endpoints ?? []).reduce((m, ep) => m + 1 + (ep.parts?.length ?? 0), 0),
    0,
  )

  return (
    <>
      <style>{`
        @keyframes pulseDot { 0%,100% { opacity:1 } 50% { opacity:.3 } }
      `}</style>

      {/* Header */}
      <div style={{ paddingTop: 28, paddingBottom: 24 }}>
        <h1 style={{ margin: '0 0 6px', fontSize: 26, fontWeight: 700, letterSpacing: '-.02em' }}>
          Matter Devices
        </h1>
        <p style={{ margin: 0, color: '#6c757d', fontSize: 14 }}>
          {loading
            ? 'Loading…'
            : `${devices.length} device${devices.length === 1 ? '' : 's'} · ${totalEps} endpoints · live readings`}
        </p>
      </div>

      {/* Error */}
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
          Failed to load devices: {error}
        </div>
      )}

      {/* Loading skeletons */}
      {loading && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 16 }}>
          {[1, 2].map((n) => (
            <div
              key={n}
              style={{
                height: 120,
                background: '#f8f9fa',
                borderRadius: 14,
                border: '1px solid #e9ecef',
                opacity: 1 - n * 0.25,
              }}
            />
          ))}
        </div>
      )}

      {/* Device cards */}
      {!loading && !error && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 20, paddingBottom: 48 }}>
          {devices.length === 0 ? (
            <div
              style={{
                border: '1.5px dashed #dee2e6',
                borderRadius: 14,
                background: '#f8f9fa',
                padding: '48px 24px',
                textAlign: 'center',
              }}
            >
              <div style={{ fontSize: 16, fontWeight: 600, marginBottom: 6 }}>No devices configured</div>
              <div style={{ fontSize: 14, color: '#6c757d' }}>
                Add a Modbus TCP device to start seeing live readings here.
              </div>
            </div>
          ) : (
            devices.map((d) => <DeviceCard key={d.id} device={d} />)
          )}
        </div>
      )}
    </>
  )
}

export default Home
