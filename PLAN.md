# BattleBots Remote Control — Build Plan

## Architecture (Final)

```
Phone/Browser (anywhere — cellular, WiFi, whatever)
  ↓ HTTPS/WSS
battlebots.stayspeed.com (Mac — cloud-hosted)
  ├── React Dashboard (port 8100, already live via Cloudflare tunnel)
  ├── WebSocket Bridge Server (new — port 8101, proxied)
  ├── MQTT Broker — mosquitto (new — port 1883 + 9001)
  └── Cloudflare tunnel (already configured)

  ↓ MQTT over internet (with TLS + username/password auth)
ESP32-S3 (on car, connected to home WiFi with public internet)
  ↓ UART (GPIO6/7 → ATmega328P D0/D1)
ATmega328P
  ↓ PWM (D7/D9, D8/D10, D13/D3, D12/D11)
Motors
```

## Component Ownership

| Component | Machine | Status |
|-----------|---------|--------|
| MQTT Broker (mosquitto) | Mac | New |
| WebSocket Bridge Server | Mac | New |
| React Dashboard | Mac | Exists — needs rewire |
| ESP32-S3 firmware | Car | Modify: add MQTT auth + public broker URL |
| ATmega328P firmware | Car | No changes |

## User Flow

1. Tom opens `battlebots.stayspeed.com` on phone (any network)
2. Login screen — enters username/password
3. Dashboard shows registered cars and their online/offline status
4. Clicks "Claim" to take control
5. Uses virtual joystick + speed slider to drive
6. Telemetry updates live (speed, direction, uptime)
7. Clicks "Release" or navigates away → car stops

---

## Phase 1 — MQTT Broker on Mac

**Goal:** mosquitto running with auth, pm2 managed.

- Install mosquitto via Homebrew
- Create `mosquitto.conf`:
  - Listener port 1883 (MQTT TCP)
  - WebSocket listener port 9001
  - `allow_anonymous false`
  - Password file with `mosquitto_passwd`
- Create credentials:
  - `bridge` — WebSocket bridge server uses this
  - `car-01` — ESP32 car uses this
- pm2 the broker for persistence

## Phase 2 — WebSocket Bridge Server

**Goal:** New Node.js service that bridges browser ↔ MQTT.

Location: `demos/battlebots-dashboard/server/`

### Responsibilities
- Connects to local mosquitto as MQTT client
- Exposes WebSocket endpoint (port 8101)
- Handles web authentication (JWT session tokens)
- Bridges: dashboard commands → MQTT topics, MQTT telemetry → dashboard

### WebSocket Protocol

```
Client → Server:
  { type: "auth", username, password }
  { type: "claim", robotId: "car-01" }
  { type: "cmd", robotId: "car-01", left: 180, right: 180 }
  { type: "stop", robotId: "car-01" }
  { type: "release", robotId: "car-01" }

Server → Client:
  { type: "auth_ok", token, user }
  { type: "auth_fail", reason }
  { type: "robots", list: [{ robotId, online, owner, ... }] }
  { type: "status", robotId, state, detail, owner }
  { type: "telemetry", robotId, left, right, runtimeState, uptimeMs, ... }
```

### Auth Flow
1. User sends `auth` with username/password
2. Bridge validates against `users.json`
3. Returns session token on success
4. All subsequent messages must include token
5. Bridge validates token before publishing/subscribing on MQTT

### Car Registration
- `users.json` maps users to robot IDs
- Each car has MQTT credentials (username/password) configured in firmware
- Dashboard shows only cars registered to the logged-in user

```json
{
  "tom": {
    "passwordHash": "...",
    "robots": ["car-01"]
  }
}
```

## Phase 3 — Dashboard Rewire

**Goal:** Transform mockup into functional remote control.

Changes to `demos/battlebots-dashboard/src/`:

### New Files
- `components/LoginScreen.tsx` — username/password form
- `hooks/useWebSocket.ts` — WebSocket client hook with reconnection
- `hooks/useAuth.ts` — auth state management

### Modified Files
- `App.tsx` — major rewire:
  - Login gate (show LoginScreen if not authenticated)
  - Replace hardcoded `robots` array with live data from WS
  - Joystick pad → send `{ left, right }` wheel values via WS
    - ↑ both forward (255, 255)
    - ↓ both backward (-255, -255)
    - ← left slow, right fast (-160, 160)
    - → left fast, right slow (160, -160)
    - ● stop (0, 0)
    - Diagonals: blended values
  - Speed slider → scales the ±255 range
  - Action buttons → Horn, LED, Grab/Release send WS messages
  - Telemetry panel → live data from car's `TEL` messages
  - Connection indicator (WS connected, car online, latency)
  - Claim/Release buttons with ownership state

## Phase 4 — ESP32 Firmware Update

**Goal:** Minimal changes to connect to public broker with auth.

### `config.h` Changes
```cpp
static const char *WIFI_SSID = "...";           // home WiFi
static const char *WIFI_PASSWORD = "...";        // home WiFi
static const char *MQTT_HOST = "mqtt.battlebots.stayspeed.com";  // public broker
static const uint16_t MQTT_PORT = 1883;
static const char *MQTT_USERNAME = "car-01";    // car MQTT credentials
static const char *MQTT_PASSWORD = "...";
static const char *ROBOT_ID = "car-01";
```

### `esp32_uart0_phase1_bridge.ino` Changes
- Already supports MQTT username/password — just needs config values
- If TLS needed: switch `WiFiClient` to `WiFiClientSecure`, set port 8883
- Everything else stays the same (same UART protocol, same topics)

## Phase 5 — Tunnel & Deploy

**Goal:** Make MQTT broker reachable from the car over internet.

- Add to Cloudflare tunnel:
  - `mqtt.battlebots.stayspeed.com` → `localhost:1883` (TCP)
  - Or use Cloudflare Spectrum for arbitrary TCP (if available)
- Alternative: expose port 1883 directly on Mac's public IP with firewall rules
- Dashboard + bridge already proxied via existing tunnel setup
- pm2 both the bridge server and mosquitto

## Phase 6 — Testing

### Local (no car needed)
1. Start mosquitto + bridge + dashboard
2. Login at `battlebots.stayspeed.com`
3. Run `virtual_car.py` — car appears online
4. Claim → joystick → virtual car moves
5. Telemetry updates in dashboard

### Remote (with car)
1. Flash ESP32 with updated firmware + public broker config
2. Car connects to home WiFi → connects to `mqtt.battlebots.stayspeed.com`
3. Open dashboard on phone (cellular) → car shows online
4. Claim → drive → telemetry
5. Test disconnection: car loses WiFi → dashboard shows offline, no runaway

---

## File Changes Summary

| File | Action |
|------|--------|
| `SVCar/firmware/esp32_uart0_phase1_bridge/config.h` | Edit: public broker URL, MQTT credentials |
| `SVCar/firmware/esp32_uart0_phase1_bridge/esp32_uart0_phase1_bridge.ino` | Minor: already supports auth, may need TLS |
| `SVCar/firmware/esp32_uart0_phase1_bridge/README.md` | Update: new config instructions |
| `demos/battlebots-dashboard/server/index.js` | **New:** WebSocket bridge + auth |
| `demos/battlebots-dashboard/server/package.json` | **New:** dependencies (ws, mqtt, bcrypt) |
| `demos/battlebots-dashboard/server/users.json` | **New:** user credentials + car registry |
| `demos/battlebots-dashboard/src/App.tsx` | Rewrite: login, WS client, live controls |
| `demos/battlebots-dashboard/src/hooks/useWebSocket.ts` | **New:** WS hook |
| `demos/battlebots-dashboard/src/hooks/useAuth.ts` | **New:** auth state |
| `demos/battlebots-dashboard/src/components/LoginScreen.tsx` | **New:** login UI |
| `demos/battlebots-dashboard/vite.config.ts` | Edit: proxy WS to bridge server |

## Build Order

1. Mosquitto on Mac — foundation, testable with `mosquitto_pub/sub`
2. Bridge server — testable with virtual_car.py
3. Dashboard rewire — testable in browser
4. ESP32 firmware — needs broker reachable first
5. Tunnel — make broker accessible from car
6. End-to-end — real car, real control from phone

## Security Model (POC)

- **Dashboard login:** username/password, bcrypt-hashed
- **MQTT auth:** per-device credentials (car has its own user)
- **Session ownership:** one controller at a time, lease-based with timeout
- **Deadman switch:** car stops if no commands received within TTL (already in firmware)
- **Not production-grade** — no rate limiting, no HTTPS cert pinning, no audit log. Fine for POC.

## Known Risks

- MQTT over Cloudflare TCP tunnel may add latency (~50-100ms acceptable for RC)
- ESP32 WiFi stability — reconnect logic already in firmware
- No camera — dashboard video panels will be placeholder/removed for now
- Home WiFi dependency — car only works where it has WiFi
