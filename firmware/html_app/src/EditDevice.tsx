import { useEffect, useState, type FormEvent } from 'react'
import { useNavigate, useParams } from 'react-router'

type Device = {
  id: string
  name: string
  host: string
  port: number
  unitId: number
}

function EditDevice() {
  const { id } = useParams<{ id: string }>()
  const navigate = useNavigate()
  const [name, setName] = useState('')
  const [host, setHost] = useState('')
  const [port, setPort] = useState(502)
  const [unitId, setUnitId] = useState(1)
  const [loading, setLoading] = useState(true)
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState<string | null>(null)
  const [notFound, setNotFound] = useState(false)

  useEffect(() => {
    fetch('/api/devices')
      .then((res) => {
        if (!res.ok) throw new Error(`HTTP ${res.status}`)
        return res.json() as Promise<Device[]>
      })
      .then((devices) => {
        const device = devices.find((d) => d.id === id)
        if (!device) {
          setNotFound(true)
          return
        }
        setName(device.name)
        setHost(device.host)
        setPort(device.port)
        setUnitId(device.unitId)
      })
      .catch((e) => setError(e instanceof Error ? e.message : String(e)))
      .finally(() => setLoading(false))
  }, [id])

  const onSubmit = async (e: FormEvent) => {
    e.preventDefault()
    setSubmitting(true)
    setError(null)
    try {
      const res = await fetch(`/api/devices/${id}`, {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, host, port, unitId }),
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      navigate('/devices')
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
      setSubmitting(false)
    }
  }

  if (loading) return <p>Loading…</p>
  if (notFound) return <p className="text-danger">Device not found.</p>

  return (
    <>
      <h1>Edit Device</h1>
      <hr />
      <form onSubmit={onSubmit}>
        <div className="mb-3">
          <label htmlFor="name" className="form-label">Name</label>
          <input
            id="name"
            className="form-control"
            value={name}
            onChange={(e) => setName(e.target.value)}
            required
          />
        </div>
        <div className="mb-3">
          <label htmlFor="host" className="form-label">Host</label>
          <input
            id="host"
            className="form-control"
            value={host}
            onChange={(e) => setHost(e.target.value)}
            placeholder="192.168.1.10"
            required
          />
        </div>
        <div className="mb-3">
          <label htmlFor="port" className="form-label">Port</label>
          <input
            id="port"
            type="number"
            className="form-control"
            value={port}
            onChange={(e) => setPort(Number(e.target.value))}
            min={1}
            max={65535}
            required
          />
        </div>
        <div className="mb-3">
          <label htmlFor="unitId" className="form-label">Unit ID</label>
          <input
            id="unitId"
            type="number"
            className="form-control"
            value={unitId}
            onChange={(e) => setUnitId(Number(e.target.value))}
            min={1}
            max={247}
            required
          />
        </div>
        {error && <p className="text-danger">Failed to save: {error}</p>}
        <button type="submit" className="btn btn-primary" disabled={submitting}>
          {submitting ? 'Saving…' : 'Save'}
        </button>
      </form>
    </>
  )
}

export default EditDevice
