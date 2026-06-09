---
id: TASK-15
title: Decouple ModbusDevice from Matter device types using register-based API
status: Done
assignee: []
created_date: '2026-05-15 15:27'
updated_date: '2026-05-18 05:01'
labels: []
dependencies: []
priority: high
ordinal: 7000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
ModbusDevice currently hardcodes the register layout and Readings struct for one specific inverter. Refactor so ModbusDevice accepts a list of RegisterSpec (address + func code) at construction time and returns a list of RegisterReading (address + value) via callback. ModbusManager then owns the register-to-Matter-attribute mapping.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [x] #1 ModbusDevice has no knowledge of Matter or specific device types
- [x] #2 ModbusDevice constructor accepts std::vector<RegisterSpec>
- [x] #3 Callback delivers std::vector<RegisterReading> (address+value pairs)
- [x] #4 ModbusManager defines the register list and maps addresses to Matter attributes
- [x] #5 Existing solar power readings (voltage, current, power) still work correctly
<!-- AC:END -->

## Final Summary

<!-- SECTION:FINAL_SUMMARY:BEGIN -->
Removed the hardcoded `Readings` struct and all register `#define`s from `ModbusDevice`. Constructor now takes `std::vector<RegisterSpec>` (address + input/holding flag). The poll task groups specs by func code, does one bulk read per group, and delivers `std::vector<RegisterReading>` (address + value) via callback. `ModbusManager` owns `k_inverter_regs` (the register list) and `readings_update_cb` (the address→Matter-attribute switch), keeping all device-type knowledge out of `ModbusDevice`.
<!-- SECTION:FINAL_SUMMARY:END -->
