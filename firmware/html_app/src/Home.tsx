import { useEffect, useState } from 'react'

const CLUSTER_NAMES: Record<number, string> = {
  0x0091: 'Electrical Power Measurement',
}

const ATTRIBUTE_META: Record<number, Record<number, { label: string; format: (v: number) => string }>> = {
  0x0091: {
    0x0008: { label: 'Voltage',        format: (v) => `${(v / 1000).toFixed(1)} V`  },
    0x0005: { label: 'Active Current',  format: (v) => `${(v / 1000).toFixed(2)} A` },
    0x0002: { label: 'Active Power',    format: (v) => `${(v / 1000).toFixed(0)} W`  },
  },
}

const DEVICE_TYPE_NAMES: Record<number, string> = {
  0x0017: 'Solar Power',
  0x0510: 'Electrical Sensor',
}

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

function AttributeCard({
  label,
  value,
  format,
}: {
  label: string
  value: number | null
  format: (v: number) => string
}) {
  return (
    <div className="col-6 col-md-3">
      <div className="card text-center h-100">
        <div className="card-body py-2">
          <div className="text-muted small">{label}</div>
          <div className="fs-5 fw-semibold">{value != null ? format(value) : '—'}</div>
        </div>
      </div>
    </div>
  )
}

function AttributeRow({
  mappings,
  epReadings,
}: {
  mappings: Mapping[]
  epReadings: EndpointReading[]
}) {
  if (mappings.length === 0) return null
  return (
    <div className="row g-2">
      {mappings.map((m, i) => {
        const meta   = ATTRIBUTE_META[m.cluster]?.[m.attribute]
        const label  = meta?.label ?? `Attr 0x${m.attribute.toString(16).padStart(4, '0')}`
        const format = meta?.format ?? ((v: number) => String(v))
        const reading = epReadings.find(
          (r) => r.clusterId === m.cluster && r.attributeId === m.attribute,
        )
        return (
          <AttributeCard key={i} label={label} value={reading?.value ?? null} format={format} />
        )
      })}
    </div>
  )
}

function PartCard({
  part,
  readings,
}: {
  part: Part
  readings: EndpointReading[]
}) {
  const epReadings = readings.filter((r) => r.endpointId === part.endpointId)

  const deviceTypeLabel = part.deviceTypes
    .map((dt) => DEVICE_TYPE_NAMES[dt] ?? `0x${dt.toString(16).padStart(4, '0')}`)
    .join(' · ')

  const clusterIds = [...new Set(part.mappings.map((m) => m.cluster))]
  const clusterLabel =
    clusterIds.length === 1
      ? (CLUSTER_NAMES[clusterIds[0]] ?? `Cluster 0x${clusterIds[0].toString(16).padStart(4, '0')}`)
      : 'Multiple Clusters'

  const headerLeft = part.description ?? clusterLabel

  return (
    <div className="card mt-2 ms-3">
      <div className="card-header small d-flex justify-content-between align-items-center">
        <span className="text-muted">{headerLeft}</span>
        <div>
          {deviceTypeLabel && <span className="text-muted me-2">{deviceTypeLabel}</span>}
          {part.endpointId != null && (
            <span className="badge bg-secondary">Endpoint {part.endpointId}</span>
          )}
        </div>
      </div>
      <div className="card-body">
        <AttributeRow mappings={part.mappings} epReadings={epReadings} />
      </div>
    </div>
  )
}

function EndpointCard({
  endpoint,
  readings,
}: {
  endpoint: MatterEndpoint
  readings: EndpointReading[]
}) {
  const epReadings = readings.filter((r) => r.endpointId === endpoint.endpointId)

  const clusterIds = [...new Set(endpoint.mappings.map((m) => m.cluster))]
  const clusterLabel =
    clusterIds.length === 1
      ? (CLUSTER_NAMES[clusterIds[0]] ?? `Cluster 0x${clusterIds[0].toString(16).padStart(4, '0')}`)
      : 'Multiple Clusters'

  const deviceTypeLabel = endpoint.deviceTypes
    .map((dt) => DEVICE_TYPE_NAMES[dt] ?? `0x${dt.toString(16).padStart(4, '0')}`)
    .join(' · ')

  const headerLeft = endpoint.description ?? clusterLabel

  return (
    <div className="card mt-2">
      <div className="card-header small d-flex justify-content-between align-items-center">
        <span className="text-muted">{headerLeft}</span>
        <div>
          {deviceTypeLabel && <span className="text-muted me-2">{deviceTypeLabel}</span>}
          {endpoint.endpointId != null && (
            <span className="badge bg-secondary">Endpoint {endpoint.endpointId}</span>
          )}
        </div>
      </div>
      <div className="card-body">
        <AttributeRow mappings={endpoint.mappings} epReadings={epReadings} />
        {endpoint.parts?.map((part, i) => (
          <PartCard key={i} part={part} readings={readings} />
        ))}
      </div>
    </div>
  )
}

function DeviceCard({ device }: { device: Device }) {
  const [readings, setReadings] = useState<EndpointReading[]>([])

  useEffect(() => {
    const fetchReadings = () => {
      fetch(`/api/devices/${device.id}/readings`)
        .then((res) => (res.ok ? (res.json() as Promise<{ readings: EndpointReading[] }>) : Promise.reject()))
        .then((data) => setReadings(data.readings))
        .catch(() => {})
    }

    fetchReadings()
    const interval = setInterval(fetchReadings, 5000)
    return () => clearInterval(interval)
  }, [device.id])

  const endpoints = device.matter_structure?.endpoints ?? []

  return (
    <div className="card mb-3">
      <div className="card-header">
        <strong>{device.name}</strong>
      </div>
      <div className="card-body">
        {endpoints.length === 0 ? (
          <p className="text-muted mb-0">No endpoints configured.</p>
        ) : (
          endpoints.map((ep, i) => (
            <EndpointCard key={i} endpoint={ep} readings={readings} />
          ))
        )}
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
      .catch((e: Error) => setError(e.message))
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
