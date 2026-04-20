# Remote ATmega Firmware Update Plan

## Goal

Keep the existing remote control network path:

- ESP32 joins local Wi-Fi as a station
- ESP32 connects to the remote MQTT service
- Users operate through `battlebots.stayspeed.com`

Add a second remote capability:

- upload a new ATmega/Nano firmware from the website
- have the ESP32 download that firmware over HTTPS
- have the ESP32 flash the ATmega over UART/reset using STK500v1

This is not ATmega self-OTA. The ESP32 acts as the remote flashing gateway.

## Architecture

```text
Browser
  -> HTTPS -> battlebots.stayspeed.com frontend
  -> HTTPS -> battlebots.stayspeed.com backend
  -> MQTT control message -> remote broker
  -> MQTT subscribed by ESP32 bridge
  -> HTTPS download by ESP32 from backend storage URL
  -> UART + RESET -> ATmega bootloader (STK500v1)
```

## High-Level Flow

1. User opens the dashboard and selects a robot.
2. User uploads a `.hex` file in the web UI.
3. Backend stores the file and computes metadata:
   - `jobId`
   - `robotId`
   - `sha256`
   - `sizeBytes`
   - signed download URL
4. Backend publishes an MQTT `fw/start` command for that robot.
5. ESP32 receives the command, validates it, enters flashing mode, and stops normal drive traffic.
6. ESP32 downloads the `.hex` file from the signed HTTPS URL.
7. ESP32 parses Intel HEX into an image buffer.
8. ESP32 flashes the ATmega via STK500v1.
9. ESP32 publishes progress and final result to MQTT.
10. Backend relays status to the dashboard UI.

## MQTT Contract

All firmware update traffic should live in a separate namespace from motion control.

Robot-specific topics:

- `robot/<robotId>/fw/start`
- `robot/<robotId>/fw/status`
- `robot/<robotId>/fw/log`
- Optional later: `robot/<robotId>/fw/cancel`

### `fw/start` payload

```json
{
  "jobId": "fw-2026-04-20-001",
  "robotId": "car-01",
  "controllerId": "tom-web",
  "url": "https://battlebots.stayspeed.com/api/fw/jobs/fw-2026-04-20-001/download?sig=...",
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "sizeBytes": 18742,
  "baud": 115200,
  "target": "atmega328p",
  "format": "intel_hex"
}
```

### `fw/status` payload

```json
{
  "jobId": "fw-2026-04-20-001",
  "robotId": "car-01",
  "state": "flashing",
  "detail": "writing_pages",
  "progress": 61,
  "writtenPages": 75,
  "totalPages": 122
}
```

Allowed `state` values:

- `accepted`
- `busy`
- `downloading`
- `downloaded`
- `verifying`
- `flashing`
- `rebooting`
- `done`
- `error`

### `fw/log` payload

Short human-readable events for debugging:

```json
{
  "jobId": "fw-2026-04-20-001",
  "robotId": "car-01",
  "level": "info",
  "message": "stk sync ok"
}
```

## ESP32 Firmware Plan

Target file:

- `SVCar/firmware/esp32_uart0_phase1_bridge/esp32_uart0_phase1_bridge.ino`

Reference implementation to reuse:

- `SVCar/doc/esp32WG.ino`

### Scope

Add a firmware update mode to the existing MQTT bridge. Reuse only the useful flashing pieces from `esp32WG.ino`:

- Intel HEX parsing
- image buffer handling
- STK500v1 flashing routine
- reset-pin control

Do not reuse:

- AP mode
- local HTTP upload API
- local dashboard assumptions

### Firmware Work Items

1. Add firmware-update topics to `buildTopics()`.
2. Add a flashing state machine:
   - `IDLE`
   - `PREPARING`
   - `DOWNLOADING`
   - `VERIFYING`
   - `FLASHING`
   - `DONE`
   - `ERROR`
3. Add a handler for `robot/<id>/fw/start`.
4. Validate the incoming job:
   - correct `robotId`
   - not already flashing
   - acceptable `target`
   - acceptable `format`
5. Publish `accepted` or `busy`.
6. Enter flashing mode:
   - send robot stop
   - clear motion owner
   - reject motion/claim traffic while update is active
   - suspend periodic runtime UART traffic such as `PING`
7. Download the hex file over HTTPS from the provided signed URL.
8. Verify:
   - HTTP success
   - size
   - SHA256
   - parseable Intel HEX
9. Flash the ATmega using STK500v1 over existing UART/reset wiring.
10. Publish progress during download and flashing.
11. Publish final `done` or `error`.
12. Exit flashing mode and resume normal bridge behavior.

### Required Firmware Refactors

The current loop assumes the UART is always in runtime mode. That must be split into two exclusive modes:

- runtime bridge mode
- flashing mode

While in flashing mode:

- no `PING`
- no motion command forwarding
- no UART line parsing as robot runtime telemetry
- no claim/drive ownership updates except rejection with `busy`

### New Helpers to Add

- `bool isFirmwareUpdateActive`
- `void publishFwStatus(...)`
- `void publishFwLog(...)`
- `bool handleFwStartMessage(...)`
- `bool downloadHexOverHttps(...)`
- `bool verifySha256(...)`
- `bool parseIntelHexToImage(...)`
- `bool flashWithStk500v1(...)`

### Memory Constraints

ATmega328P flash image is 32 KB, so buffering a full 32768-byte image is acceptable on ESP32-S3.

Recommended approach:

- download the `.hex` text into a temporary string or stream buffer
- parse into a fixed 32 KB image buffer filled with `0xFF`
- discard the raw text after parsing

### Suggested Safety Rules

- require the robot to be online before a job is accepted
- reject updates when another update is already running
- reject motion commands while flashing with a clear `busy` status
- always send stop before reset/flashing
- require SHA256 match before writing any page
- require `target == atmega328p`
- require `format == intel_hex`

### Suggested Config Additions

In `config.h` or `config.example.h`:

- `FW_DOWNLOAD_TIMEOUT_MS`
- `FW_STATUS_INTERVAL_MS`
- `FW_MAX_HEX_SIZE`
- optional `FW_ALLOW_INSECURE_TLS` only if needed for bring-up

## Frontend and Deployed Service Plan

The browser should not publish directly to MQTT. The deployed site already uses a backend/bridge layer and should continue to do so.

The deployment side has two parts:

- frontend UI in `battlebots.stayspeed.com`
- backend service behind that UI

### Frontend UI Work Items

1. Add a firmware update panel on the robot details view.
2. Require robot selection before enabling upload.
3. Add file picker limited to `.hex`.
4. Show selected file name, size, and target robot.
5. Add an `Upload and Flash` action.
6. Show update job state:
   - pending
   - accepted
   - downloading
   - flashing
   - done
   - error
7. Disable claim/drive controls while a flash job is active for that robot.
8. Show human-readable progress and logs.
9. Preserve current remote control behavior when no flash job is active.

### Backend Service Work Items

1. Accept uploaded `.hex` file from the frontend.
2. Validate:
   - extension is `.hex`
   - reasonable size
   - parsable Intel HEX if backend validation is added
3. Store the file in backend-controlled storage.
4. Compute SHA256 and file size.
5. Create a short-lived signed HTTPS download URL for the ESP32.
6. Publish `robot/<id>/fw/start` to MQTT.
7. Subscribe to `robot/<id>/fw/status` and `robot/<id>/fw/log`.
8. Relay those updates to the browser over the existing WebSocket path.
9. Persist job history for operator visibility and debugging.

### Backend API Shape

Suggested endpoints:

- `POST /api/robots/:robotId/firmware`
- `GET /api/robots/:robotId/firmware/jobs/:jobId`
- `GET /api/fw/jobs/:jobId/download?sig=...`

Suggested upload response:

```json
{
  "ok": true,
  "jobId": "fw-2026-04-20-001",
  "robotId": "car-01",
  "state": "queued"
}
```

### WebSocket Events to Browser

Suggested events from backend to frontend:

```json
{ "type": "fw_status", "robotId": "car-01", "jobId": "fw-2026-04-20-001", "state": "downloading", "progress": 34 }
{ "type": "fw_log", "robotId": "car-01", "jobId": "fw-2026-04-20-001", "level": "info", "message": "stk sync ok" }
```

## Collaboration Contract

This is the minimum contract both sides must implement consistently.

### Backend guarantees

- signed HTTPS download URL is reachable by the ESP32 over the public internet
- URL lifetime is long enough for the full job
- `sha256` and `sizeBytes` are correct
- `fw/start` JSON matches the agreed schema

### ESP32 guarantees

- publishes `accepted` or `busy` quickly after receiving `fw/start`
- publishes progress updates during long operations
- does not drive the robot while flashing
- publishes a terminal `done` or `error`

### Frontend guarantees

- robot must be explicitly selected
- user can see current job state and failure reason
- control UI reflects that flashing temporarily blocks driving

## Failure Cases

Expected failures that must be surfaced clearly:

- HTTPS download failed
- checksum mismatch
- malformed HEX
- STK500 sync failed
- page program failed
- MQTT disconnected during update
- robot busy with another flash job

Recommended `detail` strings:

- `download_failed`
- `sha256_mismatch`
- `bad_hex`
- `stk_sync_failed`
- `page_write_failed`
- `mqtt_lost`
- `already_flashing`

## Rollout Plan

1. Implement backend upload + signed download URL + MQTT start command.
2. Implement ESP32 download and parse only, without flashing.
3. Verify ESP32 can report:
   - accepted
   - downloading
   - downloaded
   - verifying
4. Integrate STK500 flashing on ESP32.
5. Test against a bench ATmega/Nano first.
6. Add frontend UI once the backend and firmware contract is stable.
7. Run end-to-end remote tests on a non-critical target board.
8. Only then allow production robot firmware updates.

## Bench Test Matrix

Minimum tests:

- upload valid `.hex` -> full success
- upload malformed `.hex` -> parse reject
- wrong SHA256 -> reject before flashing
- board disconnected -> STK sync failure
- wrong baud -> sync failure
- network drop during download -> download failure
- claim/drive attempt during flashing -> busy/rejected
- successful flash -> robot returns to normal MQTT bridge mode

## Recommendation

Implement the deployment side first:

- backend upload
- signed download URL
- MQTT job start
- status/log relay

Then implement the ESP32 side using the proven STK500 routine from `esp32WG.ino`.

Do not implement firmware transfer as raw MQTT chunks unless HTTPS download proves impossible. HTTPS download is simpler, easier to debug, and more reliable for firmware-sized payloads.
