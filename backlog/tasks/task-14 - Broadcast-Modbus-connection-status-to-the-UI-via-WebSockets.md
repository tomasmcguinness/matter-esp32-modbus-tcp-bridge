---
id: TASK-14
title: Broadcast Modbus connection status to the UI via WebSockets
status: To Do
assignee: []
created_date: '2026-04-24 05:51'
labels:
  - websocket
  - ui
  - modbus
dependencies:
  - TASK-12
priority: medium
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The UI needs real-time visibility into the connection state of each Modbus device. When the ModbusManager changes the state of a device connection (e.g. connecting, connected, disconnected), it should broadcast a JSON message over a WebSocket to all connected clients. The UI can then update the device list to reflect current status without polling.

Agreed states to broadcast: connecting, connected, disconnected.

The WebSocket endpoint should be added to the existing web server. The message payload should include the device ID and the new status string so the UI can match it to the correct entry in the device list.
<!-- SECTION:DESCRIPTION:END -->

## Acceptance Criteria
<!-- AC:BEGIN -->
- [ ] #1 When a Modbus connection attempt begins, a WebSocket message is broadcast with status 'connecting' and the device ID
- [ ] #2 When a Modbus connection succeeds, a WebSocket message is broadcast with status 'connected' and the device ID
- [ ] #3 When a Modbus connection is lost or fails, a WebSocket message is broadcast with status 'disconnected' and the device ID
- [ ] #4 The WebSocket endpoint is served by the existing ESP-IDF HTTP server (ws upgrade)
- [ ] #5 Multiple simultaneous UI clients all receive the broadcast
- [ ] #6 The JSON payload contains at minimum: { "device_id": "...", "status": "..." }
- [ ] #7 The UI device list reflects the received status in real time without requiring a page refresh
<!-- AC:END -->
