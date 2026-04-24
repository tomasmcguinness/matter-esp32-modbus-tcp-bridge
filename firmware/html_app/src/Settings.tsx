import { useState } from 'react'

type ResetState = 'idle' | 'confirming' | 'resetting' | 'done'

function Settings() {
  const [resetState, setResetState] = useState<ResetState>('idle')
  const [error, setError] = useState<string | null>(null)

  const onResetClick = () => setResetState('confirming')
  const onCancel = () => setResetState('idle')

  const onConfirm = async () => {
    setResetState('resetting')
    setError(null)
    try {
      const res = await fetch('/api/factory-reset', { method: 'POST' })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setResetState('done')
    } catch (e) {
      setError(e instanceof Error ? e.message : String(e))
      setResetState('idle')
    }
  }

  return (
    <>
      <h1>Settings</h1>
      <hr />

      <h5>Factory Reset</h5>
      <p className="text-muted">
        Removes all Matter commissioning data and all configured Modbus devices.
        The device will reboot and return to its unconfigured state.
      </p>

      {resetState === 'idle' && (
        <button className="btn btn-danger" onClick={onResetClick}>
          Factory Reset
        </button>
      )}

      {resetState === 'confirming' && (
        <div className="alert alert-warning d-flex align-items-center gap-3" role="alert">
          <span>This cannot be undone. Are you sure?</span>
          <button className="btn btn-danger btn-sm" onClick={onConfirm}>Confirm Reset</button>
          <button className="btn btn-secondary btn-sm" onClick={onCancel}>Cancel</button>
        </div>
      )}

      {resetState === 'resetting' && (
        <p className="text-muted">Resetting&hellip;</p>
      )}

      {resetState === 'done' && (
        <div className="alert alert-success" role="alert">
          Reset complete. The device is rebooting — refresh this page once it is back online.
        </div>
      )}

      {error && <p className="text-danger mt-2">{error}</p>}
    </>
  )
}

export default Settings
