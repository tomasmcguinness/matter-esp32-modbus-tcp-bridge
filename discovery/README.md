# Matter Structure Builder

A small guided web app for authoring the `matter_structure` payload that the
Modbus → Matter bridge firmware expects. Instead of hand-writing cluster and
attribute IDs, you pick friendly labels (Voltage, Active Power, Battery %…),
supply the Modbus register address, and the tool emits the correct JSON.

## Run

Use a virtual environment — most distros mark the system Python as
"externally managed" (PEP 668) and refuse a direct `pip install`.

```bash
cd discovery
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python app.py
# open http://127.0.0.1:5001
```

(No need to `activate` — calling `.venv/bin/python` directly is enough.)

## How to use

1. **Root endpoint** = the physical device (e.g. the inverter). Give it a
   description and its Matter device type (Solar Power for an inverter).
2. Add **register mappings**: choose the Matter attribute, the Modbus function
   code (FC04 input / FC03 holding), and the register address (decimal or
   `0x…`). The tool fills in the cluster + attribute IDs for you.
3. Add **parts** for logical sub-devices — a PV string, a second string, a
   battery. A battery is usually `Electrical Sensor + Power Source`.
4. Copy or download the JSON and paste it into the firmware's *Add Device* form
   (the `matter_structure` field).

## What it validates (so the firmware doesn't fail silently)

- **2048-byte cap** — the firmware stores `matter_structure_json` in a fixed
  2048-byte buffer (`MATTER_STRUCTURE_JSON_LEN`). The size bar tracks the
  compact form against that limit.
- **125-register read span** — the firmware bulk-reads `min..max` addresses per
  function code in a single Modbus request and silently fails above 125
  registers. The tool warns if any function code's span is too wide.
- **Missing device type** on the root endpoint.
- **Battery %** mapped without the Power Source device type on the same node.

## The output shape

```json
{
  "endpoints": [
    {
      "description": "The Inverter itself",
      "deviceTypes": [23],
      "mappings": [
        { "function": 4, "address": 0, "cluster": 144, "attribute": 4 }
      ],
      "parts": [
        {
          "description": "Power Measurement for PV1",
          "deviceTypes": [1296],
          "mappings": [ { "function": 4, "address": 3, "cluster": 144, "attribute": 4 } ]
        }
      ]
    }
  ]
}
```

Numbers are decimal (the on-device cJSON parser has no hex literals). Only the
first endpoint (`endpoints[0]`) and its `parts` are processed by the firmware.

## The catalog

`catalog.json` holds the supported device types, clusters, and attributes. It
deliberately lists **only what the firmware implements today** (see
`firmware/main/matter_manager.cpp` → `add_device_clusters` and
`firmware/main/modbus_manager.cpp`). When the firmware gains a new cluster or
attribute, add it here — no code change needed; the server re-reads the file on
each request.
