---
id: TASK-13
title: >-
  When we have connected to a Modbus device, create a corresponding Matter
  device
status: In Progress
assignee: []
created_date: '2026-04-24 05:38'
updated_date: '2026-04-24 09:24'
labels: []
dependencies: []
ordinal: 1000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
When we add a new Device, the application needs to create a corresponding Matter device in its bridge.
<!-- SECTION:DESCRIPTION:END -->

## Implementation Plan

<!-- SECTION:PLAN:BEGIN -->
For now, we will assume this device is a Solar Power Device Type from the Matter Device Library specification 1.5. 

The mandatory clusters can be implemented to begin with - Power Source and Electrical Sensor. The device will have AC power source.

Provide dummy values for all mandatory attributes, but use Grid Voltage, as it's read via Modbus.
<!-- SECTION:PLAN:END -->
