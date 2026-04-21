import { useEffect, useState } from 'react'
import { Link, useNavigate } from 'react-router'

type Device = {
  id: string
  name: string
  host: string
  port: number
  unitId: number
}

function Devices() {
  const navigate = useNavigate()
  const [devices, setDevices] = useState<Device[]>([])
  const [loading, setLoading] = useState(true)
  const [error, setError] = useState<string | null>(null)
  const [deletingId, setDeletingId] = useState<string | null>(null)

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

  const onDelete = async (device: Device) => {
    if (!confirm(`Delete "${device.name}"?`)) return
    setDeletingId(device.id)
    try {
      const res = await fetch(`/api/devices/${device.id}`, { method: 'DELETE' })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setDevices((current) => current.filter((d) => d.id !== device.id))
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
    } finally {
      setDeletingId(null)
    }
  }

  return (
    <>
      <h1>Modbus Devices</h1>
      <hr />
      <Link to="/devices/add" className="btn btn-primary mb-3">Add Device</Link>
      {loading && <p>Loading…</p>}
      {error && <p className="text-danger">Failed: {error}</p>}
      {!loading && (
        <table className="table">
          <thead>
            <tr>
              <th>Name</th>
              <th>Host</th>
              <th>Port</th>
              <th>Unit ID</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {devices.length === 0 ? (
              <tr>
                <td colSpan={5}>No devices configured.</td>
              </tr>
            ) : (
              devices.map((d) => (
                <tr
                  key={d.id}
                  onClick={() => navigate(`/devices/${d.id}/edit`)}
                  style={{ cursor: 'pointer' }}
                >
                  <td>{d.name}</td>
                  <td>{d.host}</td>
                  <td>{d.port}</td>
                  <td>{d.unitId}</td>
                  <td className="text-end">
                    <button
                      type="button"
                      className="btn btn-sm btn-danger"
                      onClick={(e) => {
                        e.stopPropagation()
                        onDelete(d)
                      }}
                      disabled={deletingId === d.id}
                    >
                      {deletingId === d.id ? 'Deleting…' : 'Delete'}
                    </button>
                  </td>
                </tr>
              ))
            )}
          </tbody>
        </table>
      )}
    </>
  )
}

export default Devices
