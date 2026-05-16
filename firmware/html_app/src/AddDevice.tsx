import { useState, type FormEvent } from 'react'
import { useNavigate } from 'react-router'

const HARDCODED_MATTER_STRUCTURE = {
  endpoints: [
    {
      description: "The Inverter itself",
      deviceTypes: [0x0017], //, 0x0510],
      mappings: [
        // This probably needs a *type* field, to help with uint vs int etc.
        { function: 4, address: 0x0000, cluster: 0x0090, attribute: 0x0004 }, // Voltage
        { function: 4, address: 0x0001, cluster: 0x0090, attribute: 0x0005 }, // ActiveCurrent
        { function: 4, address: 0x0002, cluster: 0x0090, attribute: 0x0008 }, // ActivePower
      ],
      // This "parts" section isn't processed yet, but it allows us to define multiple logical devices within one physical device. 
      // For a solar power device, this would have multiple electrical sensors to represent PV strings
      // or a battery.
      parts: [
        {
          description: "Power Measurement for PV1",
          deviceTypes: [0x0510], mappings: [
            { function: 4, address: 0x0000, cluster: 0x0090, attribute: 0x0004 }, // Voltage
            { function: 4, address: 0x0001, cluster: 0x0090, attribute: 0x0005 }, // ActiveCurrent
            { function: 4, address: 0x0002, cluster: 0x0090, attribute: 0x0008 }, // ActivePower
          ]
        },
        {
          description: "Power Measurement for PV2",
          deviceTypes: [0x0510], mappings: [
            { function: 4, address: 0x0000, cluster: 0x0090, attribute: 0x0004 }, // Voltage
            { function: 4, address: 0x0001, cluster: 0x0090, attribute: 0x0005 }, // ActiveCurrent
            { function: 4, address: 0x0002, cluster: 0x0090, attribute: 0x0008 }, // ActivePower
          ]
        }
      ]
    },
  ],
}

function AddDevice() {
  const navigate = useNavigate()
  const [name, setName] = useState('Solax Inverter')
  const [host, setHost] = useState('192.168.1.164')
  const [port, setPort] = useState(502)
  const [unitId, setUnitId] = useState(1)
  const [submitting, setSubmitting] = useState(false)
  const [error, setError] = useState<string | null>(null)

  const onSubmit = async (e: FormEvent) => {
    e.preventDefault()
    setSubmitting(true)
    setError(null)
    try {
      const res = await fetch('/api/devices', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, host, port, unitId, matter_structure: HARDCODED_MATTER_STRUCTURE }),
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      navigate('/devices')
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
      setSubmitting(false)
    }
  }

  return (
    <>
      <h1>Add Device</h1>
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
        {error && <p className="text-danger">Failed to add: {error}</p>}
        <button type="submit" className="btn btn-primary" disabled={submitting}>
          {submitting ? 'Adding…' : 'Add Device'}
        </button>
      </form>
    </>
  )
}

export default AddDevice
