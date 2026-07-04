import { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router'

// ── Types (mirrors the full server shape from handlers.ts) ─────────────────

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
  host: string
  port: number
  unitId: number
  matter_structure: { endpoints: MatterEndpoint[] }
}

// ── Matter device-type code → friendly label ───────────────────────────────

const DEVICE_TYPE_NAMES: Record<number, string> = {
  0x0017: 'Solar Power',
  0x0018: 'Battery Storage',
  0x0011: 'Power Source',
  0x0510: 'Electrical Sensor',
}

function getChips(device: Device): string[] {
  const seen = new Set<number>()
  for (const ep of device.matter_structure?.endpoints ?? []) {
    for (const t of ep.deviceTypes ?? []) seen.add(t)
  }
  return [...seen].map(
    (t) => DEVICE_TYPE_NAMES[t] ?? `0x${t.toString(16).toUpperCase().padStart(4, '0')}`,
  )
}

// ── Component ──────────────────────────────────────────────────────────────

function Devices() {
  const navigate = useNavigate()
  const [devices, setDevices] = useState<Device[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [deletingId, setDeletingId] = useState<string | null>(null)
  const [query, setQuery] = useState('')

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

  const onDelete = async (device: Device) => {
    if (!confirm(`Delete "${device.name}"?`)) return
    setDeletingId(device.id)
    try {
      const res = await fetch(`/api/devices/${device.id}`, { method: 'DELETE' })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setDevices((prev) => prev.filter((d) => d.id !== device.id))
    } catch (e: unknown) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setDeletingId(null)
    }
  }

  const filtered = query.trim()
    ? devices.filter(
        (d) =>
          d.name.toLowerCase().includes(query.toLowerCase()) ||
          d.host.toLowerCase().includes(query.toLowerCase()),
      )
    : devices

  // ── Render ───────────────────────────────────────────────────────────────

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
            Modbus Devices
          </h1>
          <p style={{ margin: 0, color: '#6c757d', fontSize: 14 }}>
            {loading
              ? 'Loading…'
              : `${devices.length} device${devices.length === 1 ? '' : 's'} configured`}
          </p>
        </div>
        <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
          {/* Search */}
          <div style={{ position: 'relative' }}>
            <svg
              style={{
                position: 'absolute',
                left: 11,
                top: '50%',
                transform: 'translateY(-50%)',
                pointerEvents: 'none',
              }}
              width="14"
              height="14"
              viewBox="0 0 14 14"
              fill="none"
            >
              <circle cx="6" cy="6" r="4.5" stroke="#adb5bd" strokeWidth="1.5" />
              <path d="M9.5 9.5L12.5 12.5" stroke="#adb5bd" strokeWidth="1.5" strokeLinecap="round" />
            </svg>
            <input
              type="search"
              value={query}
              onChange={(e) => setQuery(e.target.value)}
              placeholder="Search name or host"
              style={{
                height: 40,
                width: 220,
                border: '1px solid #dee2e6',
                borderRadius: 8,
                padding: '0 12px 0 32px',
                fontSize: 14,
                fontFamily: 'inherit',
                color: '#212529',
                background: '#fff',
                outline: 'none',
              }}
            />
          </div>
          {/* Add button */}
          <Link
            to="/devices/add"
            style={{
              display: 'inline-flex',
              alignItems: 'center',
              gap: 7,
              height: 40,
              padding: '0 18px',
              background: '#0d6efd',
              color: '#fff',
              borderRadius: 8,
              fontSize: 14,
              fontWeight: 600,
              textDecoration: 'none',
              whiteSpace: 'nowrap',
              boxShadow: '0 1px 2px rgba(13,110,253,.3)',
            }}
          >
            <span style={{ fontSize: 18, lineHeight: 1, marginTop: -1 }}>+</span>
            Add Device
          </Link>
        </div>
      </div>

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
      {loading && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          {[1, 2, 3].map((n) => (
            <div
              key={n}
              style={{
                height: 72,
                background: '#f8f9fa',
                borderRadius: 10,
                border: '1px solid #e9ecef',
                opacity: 1 - n * 0.2,
              }}
            />
          ))}
        </div>
      )}

      {/* Device rows */}
      {!loading && filtered.length > 0 && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 10 }}>
          {filtered.map((d) => (
            <DeviceRow
              key={d.id}
              device={d}
              chips={getChips(d)}
              deleting={deletingId === d.id}
              onEdit={() => navigate(`/devices/${d.id}/edit`)}
              onDelete={() => onDelete(d)}
            />
          ))}
        </div>
      )}

      {/* Empty states */}
      {!loading && devices.length === 0 && (
        <EmptyState
          title="No devices configured"
          body="Add a Modbus TCP device to start mapping registers to Matter clusters."
          showAdd
        />
      )}
      {!loading && devices.length > 0 && filtered.length === 0 && (
        <EmptyState
          title="No matches"
          body={`No device matches "${query}". Try a different name or host.`}
          showAdd={false}
        />
      )}
    </div>
  )
}

// ── DeviceRow ──────────────────────────────────────────────────────────────

type DeviceRowProps = {
  device: Device
  chips: string[]
  deleting: boolean
  onEdit: () => void
  onDelete: () => void
}

function DeviceRow({ device: d, chips, deleting, onEdit, onDelete }: DeviceRowProps) {
  return (
    <div
      style={{
        position: 'relative',
        display: 'flex',
        alignItems: 'center',
        gap: 20,
        background: '#fff',
        border: '1px solid #e9ecef',
        borderRadius: 10,
        overflow: 'hidden',
        padding: '16px 16px 16px 22px',
        boxShadow: '0 1px 2px rgba(0,0,0,.04)',
      }}
    >
      {/* Left accent bar */}
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

      {/* Name + connection */}
      <div style={{ minWidth: 200, flex: '0 0 auto' }}>
        <div style={{ fontSize: 15, fontWeight: 600, letterSpacing: '-.01em' }}>{d.name}</div>
        <div
          style={{
            marginTop: 4,
            display: 'flex',
            alignItems: 'center',
            gap: 8,
            fontFamily: 'monospace',
            fontSize: 12.5,
            color: '#6c757d',
          }}
        >
          <span style={{ color: '#343a40' }}>
            {d.host}:{d.port}
          </span>
          <span
            style={{ width: 3, height: 3, borderRadius: '50%', background: '#ced4da', flexShrink: 0 }}
          />
          <span>unit {d.unitId}</span>
        </div>
      </div>

      {/* Type chips */}
      <div style={{ flex: '1 1 auto', display: 'flex', flexWrap: 'wrap', gap: 6 }}>
        {chips.map((label) => (
          <span
            key={label}
            style={{
              fontSize: 11.5,
              fontWeight: 500,
              color: '#495057',
              background: '#f1f3f5',
              border: '1px solid #e9ecef',
              borderRadius: 6,
              padding: '3px 9px',
            }}
          >
            {label}
          </span>
        ))}
        {chips.length === 0 && (
          <span
            style={{
              fontSize: 12,
              color: '#adb5bd',
              fontStyle: 'italic',
            }}
          >
            No device types
          </span>
        )}
      </div>

      {/* Actions */}
      <div style={{ display: 'flex', alignItems: 'center', gap: 6, flexShrink: 0 }}>
        <button
          type="button"
          onClick={onEdit}
          style={{
            height: 34,
            border: '1px solid #dee2e6',
            background: '#fff',
            color: '#343a40',
            fontSize: 13,
            fontWeight: 500,
            fontFamily: 'inherit',
            borderRadius: 7,
            padding: '0 15px',
            cursor: 'pointer',
          }}
        >
          Edit
        </button>
        <button
          type="button"
          onClick={onDelete}
          disabled={deleting}
          title="Delete"
          style={{
            height: 34,
            width: 34,
            border: '1px solid #ffc9c9',
            background: '#fff',
            color: deleting ? '#adb5bd' : '#c92a2a',
            fontSize: 16,
            borderRadius: 7,
            cursor: deleting ? 'default' : 'pointer',
            display: 'inline-flex',
            alignItems: 'center',
            justifyContent: 'center',
            flexShrink: 0,
          }}
        >
          {deleting ? '…' : '×'}
        </button>
      </div>
    </div>
  )
}

// ── EmptyState ─────────────────────────────────────────────────────────────

function EmptyState({
  title,
  body,
  showAdd,
}: {
  title: string
  body: string
  showAdd: boolean
}) {
  return (
    <div
      style={{
        border: '1.5px dashed #dee2e6',
        borderRadius: 12,
        background: '#f8f9fa',
        padding: '48px 24px',
        textAlign: 'center',
      }}
    >
      <div style={{ fontSize: 16, fontWeight: 600, marginBottom: 6 }}>{title}</div>
      <div style={{ fontSize: 14, color: '#6c757d', marginBottom: showAdd ? 20 : 0 }}>{body}</div>
      {showAdd && (
        <Link
          to="/devices/add"
          style={{
            display: 'inline-flex',
            alignItems: 'center',
            gap: 7,
            height: 40,
            padding: '0 18px',
            background: '#0d6efd',
            color: '#fff',
            borderRadius: 8,
            fontSize: 14,
            fontWeight: 600,
            textDecoration: 'none',
          }}
        >
          + Add Device
        </Link>
      )}
    </div>
  )
}

export default Devices
