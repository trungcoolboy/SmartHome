# Bathroom 1 Node 01 Discovery

ESP-12S temporary discovery firmware for mapping:

- 3 touch inputs
- 3 LEDs
- 2 relays

Node identity:

- `bathroom-1-node-01-discovery`

Topics:

- `smarthome/bathroom-1-node-01-discovery/availability`
- `smarthome/bathroom-1-node-01-discovery/telemetry`
- `smarthome/bathroom-1-node-01-discovery/command`
- `smarthome/bathroom-1-node-01-discovery/state`

Candidate pins scanned:

- `0, 2, 4, 5, 12, 13, 14, 15, 16`

Commands:

- `{"action":"ping"}`
- `{"action":"scan_inputs","enable":true}`
- `{"action":"scan_inputs","enable":false}`
- `{"action":"report_inputs"}`
- `{"action":"probe_output","pin":5}`
- `{"action":"set_output","pin":5,"level":true}`
- `{"action":"release_outputs"}`

Use:

1. Touch one sensor at a time and watch `state` events `input_changed`.
2. Probe one output pin at a time and observe which relay/LED reacts.
3. After mapping is known, switch back to the real application firmware.
