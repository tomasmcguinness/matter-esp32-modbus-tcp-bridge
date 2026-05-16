import { http, HttpResponse } from 'msw'

type Mapping = {
  function: number
  address: number
  cluster: number
  attribute: number
}

type MatterEndpoint = {
  endpointId?: number
  deviceTypes: number[]
  mappings: Mapping[]
}

export type Device = {
  id: string
  name: string
  host: string
  port: number
  unitId: number
  matter_structure: { endpoints: MatterEndpoint[] }
}

// const MOCK_MATTER_STRUCTURE = {
//   endpoints: [
//     {
//       endpointId: 4,
//       deviceTypes: [0x010e, 0x0510],
//       mappings: [
//         { function: 4, address: 0x0000, cluster: 0x0091, attribute: 0x0008 },
//         { function: 4, address: 0x0001, cluster: 0x0091, attribute: 0x0005 },
//         { function: 4, address: 0x0002, cluster: 0x0091, attribute: 0x0002 },
//       ],
//     },
//   ],
// }

const devices: Device[] = []

export const handlers = [
  http.get('/api/devices', () => {
    return HttpResponse.json(devices)
  }),

  http.get('/api/devices/:id/readings', ({ params }) => {
    const device = devices.find((d) => d.id === params.id)
    if (!device) return new HttpResponse(null, { status: 404 })
    const ep = device.matter_structure.endpoints[0]
    return HttpResponse.json({
      readings: [
        { endpointId: ep?.endpointId ?? 0, clusterId: 0x0091, attributeId: 0x0008, value: 243800 },
        { endpointId: ep?.endpointId ?? 0, clusterId: 0x0091, attributeId: 0x0005, value: 9500 },
        { endpointId: ep?.endpointId ?? 0, clusterId: 0x0091, attributeId: 0x0002, value: 2300000 },
      ],
    })
  }),

  http.post('/api/devices', async ({ request }) => {
    const body = (await request.json()) as Omit<Device, 'id'>
    const device: Device = {
      id: crypto.randomUUID(),
      //matter_structure: MOCK_MATTER_STRUCTURE,
      ...body,
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
    return new HttpResponse(null, { status: 200 })
  }),
]
