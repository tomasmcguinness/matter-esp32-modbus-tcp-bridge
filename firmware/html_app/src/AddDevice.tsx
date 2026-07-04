import { useEffect, useRef, useState } from 'react'
import { useNavigate } from 'react-router'

// ─────────────────────────────────────────────────────────────────────────────
// Types
// ─────────────────────────────────────────────────────────────────────────────

type Mapping = {
  function: number
  address: number
  cluster: number
  attribute: number
}

type Part = {
  description?: string
  deviceTypes: number[]
  mappings: Mapping[]
}

type MatterEndpoint = {
  description?: string
  deviceTypes: number[]
  mappings: Mapping[]
  parts?: Part[]
}

type MatterStructure = { endpoints: MatterEndpoint[] }

type ConnectState = 'idle' | 'connecting' | 'ok' | 'fail'

// ─────────────────────────────────────────────────────────────────────────────
// Metadata: how a (cluster, attribute) pair is labelled & formatted
// ─────────────────────────────────────────────────────────────────────────────

const ATTRIBUTE_META: Record<number, Record<number, { label: string; unit: string; format: (v: number) => string }>> = {
  0x0090: {
    0x0004: { label: 'Voltage',        unit: 'V', format: (v) => (v / 1000).toFixed(1) },
    0x0005: { label: 'Active Current', unit: 'A', format: (v) => (v / 1000).toFixed(2) },
    0x0008: { label: 'Active Power',   unit: 'W', format: (v) => Math.round(v / 1000).toLocaleString() },
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

const hex = (n: number, w = 4) => '0x' + (n >>> 0).toString(16).toUpperCase().padStart(w, '0')
const regKey = (m: { function: number; address: number }) => `${m.function}:${m.address}`

const SAMPLE_STRUCTURE = JSON.stringify(
  {
    endpoints: [
      {
        description: 'The inverter itself',
        deviceTypes: [0x0017],
        mappings: [
          { function: 4, address: 0x0000, cluster: 0x0090, attribute: 0x0004 },
          { function: 4, address: 0x0001, cluster: 0x0090, attribute: 0x0005 },
          { function: 4, address: 0x0002, cluster: 0x0090, attribute: 0x0008 },
        ],
        parts: [
          {
            description: 'Power Measurement for PV1',
            deviceTypes: [0x0510],
            mappings: [
              { function: 4, address: 0x0003, cluster: 0x0090, attribute: 0x0004 },
              { function: 4, address: 0x0005, cluster: 0x0090, attribute: 0x0005 },
              { function: 4, address: 0x000a, cluster: 0x0090, attribute: 0x0008 },
            ],
          },
          {
            description: 'Battery',
            deviceTypes: [0x0510, 0x0018],
            mappings: [
              { function: 4, address: 0x0014, cluster: 0x0090, attribute: 0x0004 },
              { function: 4, address: 0x0015, cluster: 0x0090, attribute: 0x0005 },
              { function: 4, address: 0x001c, cluster: 0x002f, attribute: 0x000c },
            ],
          },
        ],
      },
    ],
  },
  null,
  2,
)

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

type ValidationResult =
  | { ok: true; parsed: MatterStructure; epCount: number; mapCount: number }
  | { ok: false; err: string | null }

function validateStructure(text: string): ValidationResult {
  if (!text.trim()) return { ok: false, err: null }
  let obj: unknown
  try {
    obj = JSON.parse(text)
  } catch (e) {
    return { ok: false, err: 'Invalid JSON: ' + (e instanceof Error ? e.message : String(e)) }
  }
  const s = obj as MatterStructure
  if (!s || !Array.isArray(s.endpoints)) {
    return { ok: false, err: 'Expected an object with an "endpoints" array.' }
  }
  let mapCount = 0
  let epCount = 0
  for (const ep of s.endpoints) {
    if (!Array.isArray(ep.mappings)) return { ok: false, err: 'Each endpoint needs a "mappings" array.' }
    epCount += 1
    mapCount += ep.mappings.length
    for (const p of ep.parts ?? []) {
      epCount += 1
      mapCount += (p.mappings ?? []).length
    }
  }
  return { ok: true, parsed: s, epCount, mapCount }
}

// ─────────────────────────────────────────────────────────────────────────────
// Component
// ─────────────────────────────────────────────────────────────────────────────

function AddDevice() {
  const navigate = useNavigate()

  const [step, setStep] = useState(1)

  // Step 1 — connection
  const [host, setHost] = useState('192.168.1.164')
  const [port, setPort] = useState(502)
  const [unitId, setUnitId] = useState(1)
  const [connectState, setConnectState] = useState<ConnectState>('idle')
  const [connectError, setConnectError] = useState<string | null>(null)

  // Step 2 — mapping JSON
  const [jsonText, setJsonText] = useState('')

  // Step 3 — verify
  const [parsed, setParsed] = useState<MatterStructure | null>(null)
  const [values, setValues] = useState<Record<string, number>>({})
  const [reading, setReading] = useState(false)
  const [readError, setReadError] = useState<string | null>(null)

  const [submitting, setSubmitting] = useState(false)
  const [saveError, setSaveError] = useState<string | null>(null)

  const readAbort = useRef<AbortController | null>(null)
  useEffect(() => () => readAbort.current?.abort(), [])

  const validation = validateStructure(jsonText)

  // ── Step 1: test connectivity ──────────────────────────────────────────────
  const onConnect = async () => {
    setConnectState('connecting')
    setConnectError(null)
    try {
      // TODO(firmware + mocks/handlers.ts): add POST /api/modbus/test-connection
      // Body: { host, port, unitId } → 200 { ok: true } | 502 on unreachable.
      const res = await fetch('/api/modbus/test-connection', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ host, port, unitId }),
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      setConnectState('ok')
    } catch (e) {
      setConnectState('fail')
      setConnectError(e instanceof Error ? e.message : String(e))
    }
  }

  // ── Step 3: read live register values ───────────────────────────────────────
  const onReadValues = async () => {
    if (!parsed) return
    readAbort.current?.abort()
    const ctrl = new AbortController()
    readAbort.current = ctrl

    // Collect every register referenced in the structure (de-duplicated).
    const seen = new Set<string>()
    const registers: { function: number; address: number }[] = []
    const collect = (mappings: Mapping[]) => {
      for (const m of mappings) {
        const k = regKey(m)
        if (seen.has(k)) continue
        seen.add(k)
        registers.push({ function: m.function, address: m.address })
      }
    }
    for (const ep of parsed.endpoints) {
      collect(ep.mappings)
      for (const p of ep.parts ?? []) collect(p.mappings)
    }

    setReading(true)
    setReadError(null)
    setValues({})
    try {
      // TODO(firmware + mocks/handlers.ts): add POST /api/modbus/read
      // Body: { host, port, unitId, registers: [{ function, address }] }
      // → 200 { values: [{ function, address, value }] }
      const res = await fetch('/api/modbus/read', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ host, port, unitId, registers }),
        signal: ctrl.signal,
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      const data = (await res.json()) as { values: { function: number; address: number; value: number }[] }
      const next: Record<string, number> = {}
      for (const v of data.values) next[regKey(v)] = v.value
      setValues(next)
    } catch (e) {
      if ((e as Error).name !== 'AbortError') {
        setReadError(e instanceof Error ? e.message : String(e))
      }
    } finally {
      setReading(false)
    }
  }

  // ── Save ────────────────────────────────────────────────────────────────────
  const onSave = async () => {
    if (!parsed) return
    setSubmitting(true)
    setSaveError(null)
    try {
      const res = await fetch('/api/devices', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name, host, port, unitId, matter_structure: parsed }),
      })
      if (!res.ok) throw new Error(`HTTP ${res.status}`)
      navigate('/devices')
    } catch (e) {
      setSaveError(e instanceof Error ? e.message : String(e))
      setSubmitting(false)
    }
  }

  const readCount = Object.keys(values).length

  // ── Render ──────────────────────────────────────────────────────────────────
  return (
    <div style={{ paddingTop: 28, paddingBottom: 64 }}>
      <style>{`
        @keyframes addDeviceSpin { to { transform: rotate(360deg) } }
        @keyframes addDeviceFadeUp { from { opacity:0; transform:translateY(6px) } to { opacity:1; transform:none } }
        @keyframes addDevicePulse { 0%,100% { opacity:1 } 50% { opacity:.3 } }
        @keyframes addDevicePop { from { opacity:0; transform:scale(.9) } to { opacity:1; transform:none } }
      `}</style>

      <h1 style={{ margin: '0 0 6px', fontSize: 26, fontWeight: 700, letterSpacing: '-.02em' }}>Add Device</h1>
      <p style={{ margin: '0 0 28px', color: '#6c757d', fontSize: 14 }}>
        Connect to a Modbus TCP device, paste its mapping, then verify the registers return live values.
      </p>

      <StepIndicator step={step} labels={['Connect', 'Mapping', 'Verify']} />

      {/* ── STEP 1 ── */}
      {step === 1 && (
        <Card>
          <SectionTitle title="Connection details" subtitle="Enter the address of the Modbus TCP device. The adapter will attempt to read a register to verify connectivity." />

          <div style={{ display: 'grid', gridTemplateColumns: '1fr 120px 100px', gap: 12, marginBottom: 16 }}>
            <Field label="Host">
              <TextInput value={host} onChange={(v) => { setHost(v); setConnectState('idle') }} placeholder="192.168.1.10" mono />
            </Field>
            <Field label="Port">
              <TextInput type="number" value={String(port)} onChange={(v) => { setPort(Number(v) || 502); setConnectState('idle') }} mono />
            </Field>
            <Field label="Unit ID">
              <TextInput type="number" value={String(unitId)} onChange={(v) => { setUnitId(Number(v) || 1); setConnectState('idle') }} mono />
            </Field>
          </div>

          {connectState !== 'idle' && (
            <StatusBanner
              tone={connectState === 'ok' ? 'ok' : connectState === 'fail' ? 'fail' : 'info'}
              spinner={connectState === 'connecting'}
              message={
                connectState === 'connecting'
                  ? `Connecting to ${host}:${port} unit ${unitId}…`
                  : connectState === 'ok'
                    ? `Connected · ${host}:${port} responded to register read`
                    : `Could not reach ${host}:${port}${connectError ? ` — ${connectError}` : ' — check the host and try again'}`
              }
            />
          )}

          <div style={{ display: 'flex', gap: 10, marginTop: 8 }}>
            <PrimaryButton onClick={onConnect} disabled={connectState === 'connecting'}>
              {connectState === 'connecting' ? 'Connecting…' : connectState === 'ok' ? 'Reconnect' : 'Connect'}
            </PrimaryButton>
            {connectState === 'ok' && (
              <button
                onClick={() => setStep(2)}
                style={{ ...btnBase, background: '#2e9e6b', color: '#fff', animation: 'addDeviceFadeUp .25s ease both' }}
              >
                Paste mapping →
              </button>
            )}
          </div>
        </Card>
      )}

      {/* ── STEP 2 ── */}
      {step === 2 && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 16, animation: 'addDeviceFadeUp .2s ease both' }}>
          <ConnectionBadge host={host} port={port} unitId={unitId} />

          <Card>
            <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', gap: 12, marginBottom: 4 }}>
              <div style={{ fontSize: 15, fontWeight: 700 }}>Mapping configuration</div>
              <button
                onClick={() => setJsonText(SAMPLE_STRUCTURE)}
                style={{ border: 'none', background: 'none', color: '#0d6efd', fontSize: 12.5, fontWeight: 600, cursor: 'pointer', padding: 0 }}
              >
                Load sample
              </button>
            </div>
            <div style={{ fontSize: 13, color: '#6c757d', marginBottom: 16 }}>
              Paste the <code style={codeStyle}>matter_structure</code> JSON describing how this device's registers map to
              Matter endpoints and attributes.
            </div>

            <textarea
              value={jsonText}
              onChange={(e) => setJsonText(e.target.value)}
              spellCheck={false}
              placeholder={'{ "endpoints": [ … ] }'}
              style={{
                width: '100%',
                height: 280,
                resize: 'vertical',
                border: '1px solid ' + (validation.ok ? '#2e9e6b' : jsonText.trim() ? '#c2532f' : '#3a3c44'),
                background: '#1c1d22',
                color: '#d7d8db',
                borderRadius: 10,
                padding: '14px 16px',
                fontSize: 12.5,
                lineHeight: 1.6,
                fontFamily: 'monospace',
                outline: 'none',
              }}
            />

            {!validation.ok && validation.err && <StatusBanner tone="fail" message={validation.err} compact />}
            {validation.ok && (
              <StatusBanner
                tone="ok"
                compact
                message={`Valid — ${validation.epCount} endpoint${validation.epCount === 1 ? '' : 's'}, ${validation.mapCount} register${validation.mapCount === 1 ? '' : 's'} mapped`}
              />
            )}
          </Card>

          <div style={{ display: 'flex', justifyContent: 'flex-end', gap: 10 }}>
            <SecondaryButton onClick={() => setStep(1)}>← Back</SecondaryButton>
            <PrimaryButton
              onClick={() => {
                if (validation.ok) {
                  setParsed(validation.parsed)
                  setValues({})
                  setStep(3)
                }
              }}
              disabled={!validation.ok}
            >
              Review mapping →
            </PrimaryButton>
          </div>
        </div>
      )}

      {/* ── STEP 3 ── */}
      {step === 3 && parsed && (
        <div style={{ display: 'flex', flexDirection: 'column', gap: 16, animation: 'addDeviceFadeUp .2s ease both' }}>
          <div
            style={{
              display: 'flex',
              alignItems: 'center',
              gap: 12,
              background: '#fff',
              border: '1px solid #e7e6e2',
              borderRadius: 12,
              padding: '14px 18px',
              boxShadow: '0 1px 2px rgba(20,20,22,.04)',
              flexWrap: 'wrap',
            }}
          >
            <span style={{ ...pulseDot, flex: 'none' }} />
            <span style={{ fontFamily: 'monospace', fontSize: 12.5, color: '#6c757d' }}>
              {host}:{port} · unit {unitId}
            </span>
            <span style={{ fontSize: 12.5, color: '#9a9b9e' }}>· {summarize(parsed)}</span>
            <button
              onClick={onReadValues}
              disabled={reading}
              style={{
                marginLeft: 'auto',
                height: 36,
                border: 'none',
                background: reading ? '#8fa8e8' : '#0d6efd',
                color: '#fff',
                fontWeight: 600,
                fontSize: 13,
                borderRadius: 8,
                padding: '0 16px',
                cursor: reading ? 'default' : 'pointer',
                display: 'inline-flex',
                alignItems: 'center',
                gap: 8,
                flex: 'none',
              }}
            >
              {reading && (
                <span
                  style={{
                    width: 13,
                    height: 13,
                    border: '2px solid rgba(255,255,255,.5)',
                    borderTopColor: '#fff',
                    borderRadius: '50%',
                    animation: 'addDeviceSpin .7s linear infinite',
                    display: 'inline-block',
                  }}
                />
              )}
              {reading ? 'Reading…' : readCount > 0 ? 'Read again' : 'Read live values'}
            </button>
          </div>

          {readError && <StatusBanner tone="fail" message={`Failed to read registers: ${readError}`} compact />}

          {parsed.endpoints.map((ep, i) => (
            <EndpointCard key={i} endpoint={ep} values={values} />
          ))}

          {saveError && <StatusBanner tone="fail" message={`Failed to add device: ${saveError}`} compact />}

          <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'flex-end', gap: 10 }}>
            <SecondaryButton onClick={() => setStep(2)}>← Edit mapping</SecondaryButton>
            <PrimaryButton onClick={onSave} disabled={submitting}>
              {submitting ? 'Adding…' : 'Add Device'}
            </PrimaryButton>
          </div>
        </div>
      )}
    </div>
  )
}

// ─────────────────────────────────────────────────────────────────────────────
// Presentational helpers
// ─────────────────────────────────────────────────────────────────────────────

function summarize(s: MatterStructure): string {
  let eps = 0
  let maps = 0
  for (const ep of s.endpoints) {
    eps += 1
    maps += ep.mappings.length
    for (const p of ep.parts ?? []) {
      eps += 1
      maps += (p.mappings ?? []).length
    }
  }
  return `${eps} endpoint${eps === 1 ? '' : 's'} · ${maps} register${maps === 1 ? '' : 's'}`
}

function chipsFor(deviceTypes: number[] = []): string[] {
  return deviceTypes.map((dt) => DEVICE_TYPE_NAMES[dt] ?? hex(dt))
}

function StepIndicator({ step, labels }: { step: number; labels: string[] }) {
  return (
    <div style={{ display: 'flex', alignItems: 'center', marginBottom: 32 }}>
      {labels.map((label, i) => {
        const n = i + 1
        const active = step === n
        const done = step > n
        return (
          <div key={label} style={{ display: 'flex', alignItems: 'center', flex: '1 1 0', minWidth: 0 }}>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10, flex: 'none' }}>
              <div
                style={{
                  width: 30,
                  height: 30,
                  borderRadius: '50%',
                  background: done ? '#0d6efd' : active ? '#fff' : '#f1f3f5',
                  border: '2px solid ' + (done || active ? '#0d6efd' : '#dee2e6'),
                  display: 'flex',
                  alignItems: 'center',
                  justifyContent: 'center',
                  flex: 'none',
                  transition: 'all .2s',
                }}
              >
                <span style={{ fontSize: 12, fontWeight: 700, color: done ? '#fff' : active ? '#0d6efd' : '#9a9b9e' }}>
                  {n}
                </span>
              </div>
              <span
                style={{
                  fontSize: 13,
                  fontWeight: active ? 700 : 400,
                  color: active ? '#212529' : done ? '#0d6efd' : '#9a9b9e',
                  whiteSpace: 'nowrap',
                }}
              >
                {label}
              </span>
            </div>
            {n < labels.length && (
              <div style={{ flex: '1 1 0', height: 1, background: done ? '#0d6efd' : '#dee2e6', margin: '0 12px', minWidth: 20 }} />
            )}
          </div>
        )
      })}
    </div>
  )
}

function EndpointCard({ endpoint, values }: { endpoint: MatterEndpoint; values: Record<string, number> }) {
  const chips = chipsFor(endpoint.deviceTypes)
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
      <div style={{ position: 'relative', padding: '15px 20px 15px 24px', borderBottom: '1px solid #efeeea' }}>
        <div style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: 3, background: '#0d6efd' }} />
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, flexWrap: 'wrap' }}>
          <span style={{ fontSize: 15, fontWeight: 700, letterSpacing: '-.01em' }}>{endpoint.description ?? 'Endpoint'}</span>
          {chips.map((c) => (
            <Chip key={c} label={c} />
          ))}
          <span style={{ marginLeft: 'auto', fontFamily: 'monospace', fontSize: 11, color: '#a4a5a8', flex: 'none' }}>
            {endpoint.mappings.length} register{endpoint.mappings.length === 1 ? '' : 's'}
          </span>
        </div>
      </div>

      <div style={{ padding: '6px 12px' }}>
        {endpoint.mappings.map((m, i) => (
          <MappingRow key={i} mapping={m} values={values} />
        ))}
      </div>

      {(endpoint.parts ?? []).length > 0 && (
        <div style={{ padding: '0 12px 12px', display: 'flex', flexDirection: 'column', gap: 8 }}>
          {(endpoint.parts ?? []).map((p, i) => (
            <PartCard key={i} part={p} values={values} />
          ))}
        </div>
      )}
    </div>
  )
}

function PartCard({ part, values }: { part: Part; values: Record<string, number> }) {
  const chips = chipsFor(part.deviceTypes)
  return (
    <div style={{ border: '1px solid #ebebE6', borderRadius: 10, overflow: 'hidden', background: '#fbfbf9', margin: '0 12px' }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '9px 14px', borderBottom: '1px solid #efeeea', flexWrap: 'wrap' }}>
        <span style={{ width: 6, height: 6, borderRadius: '50%', border: '1.5px solid #b6b7ba', flex: 'none', display: 'inline-block' }} />
        <span style={{ fontSize: 12.5, fontWeight: 600, color: '#55565a' }}>{part.description ?? 'Sub-device'}</span>
        {chips.map((c) => (
          <Chip key={c} label={c} small />
        ))}
      </div>
      <div style={{ padding: '4px 8px' }}>
        {part.mappings.map((m, i) => (
          <MappingRow key={i} mapping={m} values={values} small />
        ))}
      </div>
    </div>
  )
}

function MappingRow({ mapping, values, small }: { mapping: Mapping; values: Record<string, number>; small?: boolean }) {
  const meta = ATTRIBUTE_META[mapping.cluster]?.[mapping.attribute]
  const raw = values[regKey(mapping)]
  const has = raw != null
  const label = meta?.label ?? `attr ${hex(mapping.attribute)}`

  if (small) {
    return (
      <div
        style={{
          display: 'grid',
          gridTemplateColumns: '90px 60px 1fr auto',
          gap: 10,
          alignItems: 'center',
          padding: '8px 10px',
          borderRadius: 7,
          background: has ? '#f6faf7' : '#fff',
          margin: '2px 0',
        }}
      >
        <span style={{ fontFamily: 'monospace', fontSize: 12.5, color: '#1b1c1e' }}>{hex(mapping.address)}</span>
        <span style={{ fontFamily: 'monospace', fontSize: 11, color: '#9a9b9e' }}>FC0{mapping.function}</span>
        <span style={{ fontSize: 12.5, color: '#3a3b3e' }}>{label}</span>
        <div style={{ textAlign: 'right', minWidth: 84 }}>
          {has ? (
            <span style={{ fontFamily: 'monospace', fontSize: 15, fontWeight: 600, color: '#1b1c1e', animation: 'addDevicePop .25s ease both' }}>
              {meta ? meta.format(raw) : String(raw)}
              <span style={{ fontSize: 10, fontWeight: 500, color: '#9a9b9e', marginLeft: 2 }}>{meta?.unit}</span>
            </span>
          ) : (
            <span style={{ fontFamily: 'monospace', fontSize: 13, color: '#cfcfca' }}>—</span>
          )}
        </div>
      </div>
    )
  }

  return (
    <div
      style={{
        display: 'grid',
        gridTemplateColumns: '90px 70px 1fr auto',
        gap: 12,
        alignItems: 'center',
        padding: '9px 12px',
        borderRadius: 8,
        background: has ? '#f6faf7' : '#fbfbf9',
        margin: '3px 0',
      }}
    >
      <span style={{ fontFamily: 'monospace', fontSize: 13, color: '#1b1c1e' }}>{hex(mapping.address)}</span>
      <span style={{ fontFamily: 'monospace', fontSize: 12, color: '#9a9b9e' }}>FC0{mapping.function}</span>
      <div style={{ minWidth: 0 }}>
        <div style={{ fontSize: 13, fontWeight: 500, color: '#3a3b3e' }}>{label}</div>
        <div style={{ fontFamily: 'monospace', fontSize: 11, color: '#a4a5a8', marginTop: 1 }}>
          cluster {hex(mapping.cluster)} · attr {hex(mapping.attribute)}
        </div>
      </div>
      <div style={{ textAlign: 'right', minWidth: 96 }}>
        {has ? (
          <>
            <div style={{ fontFamily: 'monospace', fontSize: 17, fontWeight: 600, color: '#1b1c1e', animation: 'addDevicePop .25s ease both' }}>
              {meta ? meta.format(raw) : String(raw)}
              <span style={{ fontSize: 11, fontWeight: 500, color: '#9a9b9e', marginLeft: 2 }}>{meta?.unit}</span>
            </div>
            <div style={{ fontFamily: 'monospace', fontSize: 10.5, color: '#a4a5a8', marginTop: 1 }}>raw {raw.toLocaleString()}</div>
          </>
        ) : (
          <span style={{ fontFamily: 'monospace', fontSize: 14, color: '#cfcfca' }}>—</span>
        )}
      </div>
    </div>
  )
}

function ConnectionBadge({ host, port, unitId }: { host: string; port: number; unitId: number }) {
  return (
    <div
      style={{
        display: 'flex',
        alignItems: 'center',
        gap: 10,
        background: '#fff',
        border: '1px solid #e7e6e2',
        borderRadius: 11,
        padding: '12px 18px',
        boxShadow: '0 1px 2px rgba(20,20,22,.04)',
      }}
    >
      <span style={pulseDot} />
      <span style={{ fontFamily: 'monospace', fontSize: 13, fontWeight: 600, color: '#2e9e6b' }}>Connected</span>
      <span style={{ fontFamily: 'monospace', fontSize: 12.5, color: '#6c757d' }}>
        {host}:{port} · unit {unitId}
      </span>
    </div>
  )
}

function StatusBanner({
  tone,
  message,
  spinner,
  compact,
}: {
  tone: 'ok' | 'fail' | 'info'
  message: string
  spinner?: boolean
  compact?: boolean
}) {
  const palette = {
    ok: { bg: '#e9f5ef', border: '#b5e0cb', color: '#1a6b45' },
    fail: { bg: '#fdf0ea', border: '#f5ccb0', color: '#8c3315' },
    info: { bg: '#f0f2fd', border: '#c8d2f8', color: '#0d6efd' },
  }[tone]
  return (
    <div
      style={{
        marginTop: compact ? 12 : 0,
        marginBottom: compact ? 0 : 16,
        padding: compact ? '10px 14px' : '12px 16px',
        borderRadius: compact ? 8 : 9,
        background: palette.bg,
        border: `1px solid ${palette.border}`,
        display: 'flex',
        alignItems: 'center',
        gap: 10,
        animation: 'addDeviceFadeUp .18s ease both',
      }}
    >
      {spinner && (
        <div
          style={{
            width: 14,
            height: 14,
            border: '2px solid #c6c7ca',
            borderTopColor: '#0d6efd',
            borderRadius: '50%',
            animation: 'addDeviceSpin .7s linear infinite',
            flex: 'none',
          }}
        />
      )}
      {!spinner && tone === 'ok' && <Glyph kind="ok" />}
      {!spinner && tone === 'fail' && <Glyph kind="fail" />}
      <span style={{ fontSize: 13, color: palette.color }}>{message}</span>
    </div>
  )
}

function Glyph({ kind }: { kind: 'ok' | 'fail' }) {
  if (kind === 'ok') {
    return (
      <svg width="16" height="16" viewBox="0 0 16 16" fill="none" style={{ flex: 'none' }}>
        <circle cx="8" cy="8" r="7" fill="#2e9e6b" />
        <path d="M5 8l2 2 4-4" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" strokeLinejoin="round" />
      </svg>
    )
  }
  return (
    <svg width="16" height="16" viewBox="0 0 16 16" fill="none" style={{ flex: 'none' }}>
      <circle cx="8" cy="8" r="7" fill="#c2532f" />
      <path d="M5.5 5.5l5 5M10.5 5.5l-5 5" stroke="#fff" strokeWidth="1.5" strokeLinecap="round" />
    </svg>
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

function Card({ children }: { children: React.ReactNode }) {
  return (
    <div style={{ background: '#fff', border: '1px solid #e7e6e2', borderRadius: 14, padding: 24, boxShadow: '0 1px 2px rgba(20,20,22,.04)' }}>
      {children}
    </div>
  )
}

function SectionTitle({ title, subtitle }: { title: string; subtitle: string }) {
  return (
    <>
      <div style={{ fontSize: 16, fontWeight: 700, marginBottom: 4 }}>{title}</div>
      <div style={{ fontSize: 13.5, color: '#6c757d', marginBottom: 24 }}>{subtitle}</div>
    </>
  )
}

function Field({ label, children }: { label: string; children: React.ReactNode }) {
  return (
    <div>
      <label style={{ display: 'block', fontSize: 12, fontWeight: 600, color: '#55565a', marginBottom: 6 }}>{label}</label>
      {children}
    </div>
  )
}

function TextInput({
  value,
  onChange,
  placeholder,
  type,
  mono,
}: {
  value: string
  onChange: (v: string) => void
  placeholder?: string
  type?: string
  mono?: boolean
}) {
  return (
    <input
      type={type ?? 'text'}
      value={value}
      onChange={(e) => onChange(e.target.value)}
      placeholder={placeholder}
      style={{
        width: '100%',
        height: 42,
        border: '1px solid #e3e2dd',
        background: '#fff',
        borderRadius: 9,
        padding: '0 13px',
        fontSize: 14,
        fontFamily: mono ? 'monospace' : 'inherit',
        color: '#1b1c1e',
        outline: 'none',
      }}
    />
  )
}

const btnBase: React.CSSProperties = {
  height: 42,
  border: 'none',
  fontWeight: 600,
  fontSize: 14,
  borderRadius: 9,
  padding: '0 22px',
  cursor: 'pointer',
}

function PrimaryButton({ children, onClick, disabled }: { children: React.ReactNode; onClick: () => void; disabled?: boolean }) {
  return (
    <button
      onClick={onClick}
      disabled={disabled}
      style={{ ...btnBase, background: disabled ? '#bec8f0' : '#0d6efd', color: '#fff', cursor: disabled ? 'default' : 'pointer', boxShadow: disabled ? 'none' : '0 1px 2px rgba(13,110,253,.35)' }}
    >
      {children}
    </button>
  )
}

function SecondaryButton({ children, onClick }: { children: React.ReactNode; onClick: () => void }) {
  return (
    <button onClick={onClick} style={{ ...btnBase, background: '#fff', color: '#55565a', border: '1px solid #e3e2dd', padding: '0 18px' }}>
      {children}
    </button>
  )
}

const pulseDot: React.CSSProperties = {
  width: 8,
  height: 8,
  borderRadius: '50%',
  background: '#2e9e6b',
  display: 'inline-block',
  animation: 'addDevicePulse 2s ease-in-out infinite',
}

const codeStyle: React.CSSProperties = {
  fontFamily: 'monospace',
  fontSize: '.9em',
  background: '#f1f3f5',
  borderRadius: 4,
  padding: '1px 5px',
}

export default AddDevice
