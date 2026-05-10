"""
Solax X1 G4 Hybrid Inverter — Modbus TCP Reader
Reads PV, battery, and grid registers in three contiguous blocks.

Protocol: Solax Hybrid X1/X3 G4 Modbus TCP/RTU v3.21
Default slave address: 0x01
All reads use FC 0x04 (Read Input Registers)
"""

from pymodbus.client import ModbusTcpClient
from dataclasses import dataclass

# ── Configuration ─────────────────────────────────────────────────────────────

INVERTER_HOST = "192.168.1.164"   # Change to your inverter's IP
INVERTER_PORT = 502
SLAVE_ID      = 0x01


# ── Helpers ───────────────────────────────────────────────────────────────────

def to_signed(val: int) -> int:
    """Convert an unsigned 16-bit Modbus register value to signed."""
    return val if val < 32768 else val - 65536


def read_block(client: ModbusTcpClient, start: int, count: int) -> list[int]:
    """Read a contiguous block of input registers; raises on failure."""
    result = client.read_input_registers(start, count=count, slave=SLAVE_ID)
    if result.isError():
        raise RuntimeError(f"Modbus read error at 0x{start:04X} (count={count}): {result}")
    return result.registers


# ── Data classes ──────────────────────────────────────────────────────────────

@dataclass
class PVData:
    pv1_voltage: float   # V
    pv1_current: float   # A
    pv1_power:   int     # W
    pv2_voltage: float   # V
    pv2_current: float   # A
    pv2_power:   int     # W

    @property
    def total_power(self) -> int:
        return self.pv1_power + self.pv2_power


@dataclass
class BatteryData:
    voltage:     float   # V
    current:     float   # A  (+ve = charging, -ve = discharging)
    power:       int     # W  (+ve = charging, -ve = discharging)
    soc:         int     # %
    temperature: int     # °C


@dataclass
class GridData:
    voltage:      float  # V
    current:      float  # A  (signed)
    frequency:    float  # Hz
    feedin_power: int    # W  (+ve = exporting, -ve = importing)


# ── Register reads ────────────────────────────────────────────────────────────

def read_pv(client: ModbusTcpClient) -> PVData:
    """
    Block 1: 0x0006–0x000B (6 registers)
      0x0006  PV1 Voltage   (× 0.1 V)
      0x0007  PV1 Current   (× 0.1 A)
      0x0008  PV2 Voltage   (× 0.1 V)
      0x0009  PV2 Current   (× 0.1 A)
      0x000A  PV1 Power     (W)
      0x000B  PV2 Power     (W)
    """
    r = read_block(client, 0x0006, 6)
    return PVData(
        pv1_voltage = r[0] * 0.1,
        pv1_current = r[1] * 0.1,
        pv2_voltage = r[2] * 0.1,
        pv2_current = r[3] * 0.1,
        pv1_power   = r[4],
        pv2_power   = r[5],
    )


def read_battery(client: ModbusTcpClient) -> BatteryData:
    """
    Block 2: 0x0014–0x001D (10 registers)
      0x0014  Battery Voltage   (× 0.1 V)
      0x0015  Battery Current   (× 0.1 A, signed)
      0x0016  Battery Power     (W, signed)
      0x0017–0x001B  (reserved / other)
      0x001C  Battery SOC       (%)
      0x001D  Battery Temp      (°C)
    """
    r = read_block(client, 0x0014, 10)
    return BatteryData(
        voltage     = r[0] * 0.1,
        current     = to_signed(r[1]) * 0.1,
        power       = to_signed(r[2]),
        soc         = r[8],              # 0x001C offset from 0x0014
        temperature = r[9],              # 0x001D offset from 0x0014
    )


def read_grid(client: ModbusTcpClient) -> GridData:
    """
    Block 3: 0x0020–0x0024 (5 registers)
      0x0020  Grid Voltage    (× 0.1 V)
      0x0021  Grid Current    (× 0.1 A, signed)
      0x0022  Grid Frequency  (× 0.01 Hz)
      0x0023  (reserved)
      0x0024  Feedin Power    (W, signed: +ve = export, -ve = import)
    """
    r = read_block(client, 0x0020, 5)
    return GridData(
        voltage      = r[0] * 0.1,
        current      = to_signed(r[1]) * 0.1,
        frequency    = r[2] * 0.01,
        feedin_power = to_signed(r[4]),
    )


# ── Display ───────────────────────────────────────────────────────────────────

def print_report(pv: PVData, bat: BatteryData, grid: GridData) -> None:
    print("\n═══════════════════════════════════")
    print("  Solax X1 G4 — Live Readings")
    print("═══════════════════════════════════")

    print("\n📟 PV")
    print(f"  PV1  {pv.pv1_voltage:.1f} V  {pv.pv1_current:.1f} A  →  {pv.pv1_power} W")
    print(f"  PV2  {pv.pv2_voltage:.1f} V  {pv.pv2_current:.1f} A  →  {pv.pv2_power} W")
    print(f"  Total PV Power:  {pv.total_power} W")

    print("\n🔋 Battery")
    direction = "charging" if bat.power >= 0 else "discharging"
    print(f"  {bat.voltage:.1f} V  {bat.current:.1f} A  →  {abs(bat.power)} W ({direction})")
    print(f"  SOC:  {bat.soc}%")
    print(f"  Temp: {bat.temperature}°C")

    print("\n⚡ Grid")
    flow = "exporting" if grid.feedin_power >= 0 else "importing"
    print(f"  {grid.voltage:.1f} V  {grid.current:.1f} A  {grid.frequency:.2f} Hz")
    print(f"  Feedin: {abs(grid.feedin_power)} W ({flow})")

    print()


# ── Main ──────────────────────────────────────────────────────────────────────

def main() -> None:
    client = ModbusTcpClient(INVERTER_HOST, port=INVERTER_PORT)

    if not client.connect():
        raise ConnectionError(f"Could not connect to inverter at {INVERTER_HOST}:{INVERTER_PORT}")

    try:
        pv   = read_pv(client)
        bat  = read_battery(client)
        grid = read_grid(client)
        print_report(pv, bat, grid)
    finally:
        client.close()


if __name__ == "__main__":
    main()