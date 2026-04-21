import { http, HttpResponse } from 'msw'

export type Device = {
  id: string
  name: string
  host: string
  port: number
  unitId: number
}

const devices: Device[] = []

export const handlers = [
  http.get('/api/devices', () => {
    return HttpResponse.json(devices)
  }),

  http.post('/api/devices', async ({ request }) => {
    const body = (await request.json()) as Omit<Device, 'id'>
    const device: Device = { id: crypto.randomUUID(), ...body }
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
    const [removed] = devices.splice(index, 1)
    return HttpResponse.json(removed)
  }),
]
