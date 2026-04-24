---
id: TASK-12
title: 'When a device is added, create a Modbus connection'
status: Done
assignee: []
created_date: '2026-04-24 05:38'
updated_date: '2026-04-24 06:25'
labels: []
dependencies: []
ordinal: 1000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Once a user has added a Device via the API, we need to establish a Modbus connection ot the device.
<!-- SECTION:DESCRIPTION:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
We want to try and do this in the most encapsulated way. There should be a class which represents each Modbus device. It shoudl be responsible for maintingin the modbus connection.

Over time, this class will be responsible for fetching the appropriate registers and then setting those values on the appropriate Matter endpoint/attributes.

The exact mapping of Register value to Matter attribute will be provided in a future ticket.
<!-- SECTION:NOTES:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Implemented per-device Modbus TCP connections using raw POSIX sockets. Created ModbusDevice class (lazy-connects on first read, handles partial send/recv, resets socket on error) and ModbusManager singleton (restores devices from store on boot, wired into web server for add/update/remove). Replaced the old single-instance esp-modbus master entirely. A polling task reads Grid Voltage (register 0x0000, ÷10 for volts) every 5 seconds and logs the raw value. Confirmed working on first attempt — 245.6V reading received from Solax inverter.
<!-- SECTION:FINAL_SUMMARY:END -->
