"""Matter structure builder — a small guided web app.

Helps you author the `matter_structure` JSON payload that the Modbus->Matter
bridge firmware expects (see firmware/main/matter_manager.cpp and
firmware/main/modbus_manager.cpp). The wizard (add mappings, add parts, live
preview, validation) runs in the browser; this server serves the page and the
catalog, and proxies live Modbus reads so you can check register values against
a real device while you build the structure.

The Modbus endpoints mirror the firmware's own contract
(POST /api/modbus/test-connection and /api/modbus/read in
firmware/main/web_server.cpp), so the two stay aligned.

Run:
    python3 -m venv .venv
    .venv/bin/pip install -r requirements.txt
    .venv/bin/python app.py
    # open http://127.0.0.1:5001
"""

import json
from pathlib import Path

from flask import Flask, render_template, jsonify, request
from pymodbus.client import ModbusTcpClient

BASE_DIR = Path(__file__).resolve().parent
CATALOG_PATH = BASE_DIR / "catalog.json"

# Bound each Modbus operation so an unreachable host can't hang the request.
MODBUS_TIMEOUT_S = 3
MODBUS_RETRIES = 1

app = Flask(__name__)


@app.get("/")
def index():
    return render_template("wizard.html")


@app.get("/catalog.json")
def catalog():
    # Read on each request so you can edit catalog.json without restarting.
    with CATALOG_PATH.open() as f:
        return jsonify(json.load(f))


# ---------------------------------------------------------------------------
# Live Modbus reads
# ---------------------------------------------------------------------------

def _is_number(v):
    # JSON booleans are ints in Python; reject them explicitly.
    return isinstance(v, (int, float)) and not isinstance(v, bool)


def _parse_target(body):
    """Validate the shared { host, port, unitId } fields.

    Returns ((host, port, unit_id), None) or (None, error_message).
    """
    host = body.get("host")
    port = body.get("port")
    unit = body.get("unitId")
    if not isinstance(host, str) or not host.strip():
        return None, "host must be a non-empty string"
    if not _is_number(port):
        return None, "port must be a number"
    if not _is_number(unit):
        return None, "unitId must be a number"
    return (host.strip(), int(port), int(unit)), None


@app.post("/api/modbus/test-connection")
def modbus_test_connection():
    target, err = _parse_target(request.get_json(silent=True) or {})
    if err:
        return jsonify({"ok": False, "error": err}), 400
    host, port, _unit = target

    client = ModbusTcpClient(host, port=port, timeout=MODBUS_TIMEOUT_S, retries=MODBUS_RETRIES)
    try:
        # For Modbus TCP a successful socket connect is the reachability test;
        # we don't probe a register (it may legitimately not exist on the unit).
        if client.connect():
            return jsonify({"ok": True})
        return jsonify({"ok": False, "error": f"Could not connect to {host}:{port}"}), 502
    except Exception as exc:  # pragma: no cover - defensive
        return jsonify({"ok": False, "error": str(exc)}), 502
    finally:
        client.close()


@app.post("/api/modbus/read")
def modbus_read():
    body = request.get_json(silent=True) or {}
    target, err = _parse_target(body)
    if err:
        return jsonify({"ok": False, "error": err}), 400
    host, port, unit = target

    registers = body.get("registers")
    if not isinstance(registers, list):
        return jsonify({"ok": False, "error": "registers must be an array"}), 400

    client = ModbusTcpClient(host, port=port, timeout=MODBUS_TIMEOUT_S, retries=MODBUS_RETRIES)
    try:
        if not client.connect():
            return jsonify({"ok": False, "error": f"Could not connect to {host}:{port}"}), 502

        values = []
        for r in registers:
            if not isinstance(r, dict):
                continue
            fn = r.get("function")
            addr = r.get("address")
            if not _is_number(fn) or not _is_number(addr):
                continue
            fn = int(fn)
            addr = int(addr)

            try:
                if fn == 4:
                    rr = client.read_input_registers(addr, count=1, device_id=unit)
                elif fn == 3:
                    rr = client.read_holding_registers(addr, count=1, device_id=unit)
                else:
                    continue
            except Exception:
                # A single bad read shouldn't fail the whole batch; the UI shows "—".
                continue

            if rr.isError() or not getattr(rr, "registers", None):
                continue
            values.append({"function": fn, "address": addr, "value": rr.registers[0]})

        return jsonify({"values": values})
    except Exception as exc:  # pragma: no cover - defensive
        return jsonify({"ok": False, "error": str(exc)}), 502
    finally:
        client.close()


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5001, debug=True)
