import { http, HttpResponse } from 'msw'

export type Device = {
  id: string
  name: string
  host: string
  port: number
  unitId: number
  endpointId: number
}

const devices: Device[] = []

export const handlers = [
  http.get('/api/devices', () => {
    return HttpResponse.json(devices)
  }),

  http.get('/api/devices/:id/readings', ({ params }) => {
    const device = devices.find((d) => d.id === params.id)
    if (!device) return new HttpResponse(null, { status: 404 })
    return HttpResponse.json({
      electricalPowerMeasurement: {
        voltage: 243800,
        activeCurrent: 9500,
        activePower: 2300000,
      },
    })
  }),

  http.post('/api/devices', async ({ request }) => {
    const body = (await request.json()) as Omit<Device, 'id' | 'endpointId'>
    const device: Device = { id: crypto.randomUUID(), endpointId: 0, ...body }
    devices.push(device)
    return HttpResponse.json(device, { status: 201 })
  }),

  http.put('/api/devices/:id', async ({ params, request }) => {
    const index = devices.findIndex((d) => d.id === params.id)
    if (index === -1) {
      return new HttpResponse(null, { status: 404 })
    }
    const body = (await request.json()) as Omit<Device, 'id' | 'endpointId'>
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
