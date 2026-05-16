import { http, HttpResponse } from 'msw'

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

export type Device = {
  id: string
  name: string
  host: string
  port: number
  unitId: number
  matter_structure: { endpoints: MatterEndpoint[] }
}

// Simulate the server assigning endpoint IDs to the matter structure on device creation.
let nextEndpointId = 4
function assignEndpointIds(structure: { endpoints: MatterEndpoint[] }): { endpoints: MatterEndpoint[] } {
  return {
    endpoints: structure.endpoints.map((ep) => ({
      ...ep,
      endpointId: nextEndpointId++,
      parts: ep.parts?.map((part) => ({
        ...part,
        endpointId: nextEndpointId++,
      })),
    })),
  }
}

const devices: Device[] = []

export const handlers = [
  http.get('/api/devices', () => {
    return HttpResponse.json(devices)
  }),

  http.get('/api/devices/:id/readings', ({ params }) => {
    const device = devices.find((d) => d.id === params.id)
    if (!device) return new HttpResponse(null, { status: 404 })

    const readings: { endpointId: number; clusterId: number; attributeId: number; value: number }[] = []

    for (const ep of device.matter_structure.endpoints) {
      if (ep.endpointId == null) continue
      for (const m of ep.mappings) {
        readings.push({ endpointId: ep.endpointId, clusterId: m.cluster, attributeId: m.attribute, value: mockValue(m.attribute) })
      }
      for (const part of ep.parts ?? []) {
        if (part.endpointId == null) continue
        for (const m of part.mappings) {
          readings.push({ endpointId: part.endpointId, clusterId: m.cluster, attributeId: m.attribute, value: mockValue(m.attribute) })
        }
      }
    }

    return HttpResponse.json({ readings })
  }),

  http.post('/api/devices', async ({ request }) => {
    const body = (await request.json()) as Omit<Device, 'id'>
    const device: Device = {
      id: crypto.randomUUID(),
      ...body,
      matter_structure: assignEndpointIds(body.matter_structure),
    }
    devices.push(device)
    return HttpResponse.json(device, { status: 201 })
  }),

  http.put('/api/devices/:id', async ({ params, request }) => {
    const index = devices.findIndex((d) => d.id === params.id)
    if (index === -1) {
      return new HttpResponse(null, { status: 404 })
    }
    const body = (await request.json()) as Omit<Device, 'id'>
    devices[index] = { ...devices[index], ...body }
    return HttpResponse.json(devices[index])
  }),

  http.delete('/api/devices/:id', ({ params }) => {
    const index = devices.findIndex((d) => d.id === params.id)
    if (index === -1) {
      return new HttpResponse(null, { status: 404 })
    }
    devices.splice(index, 1)
    return new HttpResponse(null, { status: 204 })
  }),

  http.get('/api/matter/pairing', () => {
    return HttpResponse.json({
      commissioned: false,
      qrCode: 'MT:Y3KY.ABG30003C00',
      manualPairingCode: '34970112332',
    })
  }),

  http.post('/api/matter/open-commissioning-window', () => {
    return new HttpResponse(null, { status: 200 })
  }),

  http.post('/api/factory-reset', () => {
    devices.splice(0, devices.length)
    nextEndpointId = 4
    return new HttpResponse(null, { status: 200 })
  }),
]

function mockValue(attributeId: number): number {
  if (attributeId === 0x0008) return 243800   // ~243.8 V
  if (attributeId === 0x0005) return 9500     // ~9.5 A
  if (attributeId === 0x0002) return 2300000  // ~2300 W
  return 0
}
