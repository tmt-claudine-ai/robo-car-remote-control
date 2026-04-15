# SVCar Remote Control

This directory contains the final useful subset of the SVCar Phase 1 work: a working remote-control path from a PC teleop client, through MQTT and the ESP32-S3 gateway, into the ATmega328P chassis controller.

## What is included

- `firmware/esp32_uart0_phase1_bridge/`
  - ESP32-S3 bridge firmware for Wi-Fi, MQTT, session control, and UART forwarding
- `firmware/nano_phase1_uart0_runtime/`
  - ATmega328P runtime for motor control, telemetry, and timeout stop
- `tools/`
  - local broker runner
  - keyboard teleop client
  - virtual car simulator
- `scripts/`
  - local Python environment setup
  - broker start/stop helpers
- `doc/`
  - current technical findings
  - Phase 1 bring-up plan
  - reference gateway/uploader sketches
  - `Tai_finder_X1` motherboard reference code

## Phase 1 architecture

```text
PC teleop -> MQTT broker -> ESP32-S3 -> UART -> ATmega328P -> chassis
```

The final working hardware path uses the ESP32-S3 as the Wi-Fi/MQTT gateway and the ATmega328P as the low-level motor controller.

## Quick start

### 1. Prepare the local tool environment

From this directory:

```powershell
.\scripts\setup_env.ps1
```

### 2. Start the local MQTT broker

```powershell
.\scripts\start_local_broker.ps1
```

### 3. Flash the boards

- Flash `firmware/nano_phase1_uart0_runtime/nano_phase1_uart0_runtime.ino` to the ATmega328P/Nano
- Flash `firmware/esp32_uart0_phase1_bridge/esp32_uart0_phase1_bridge.ino` to the ESP32-S3

Before flashing the ESP32, copy `firmware/esp32_uart0_phase1_bridge/config.example.h`
to `firmware/esp32_uart0_phase1_bridge/config.h`, then fill in:

- Wi-Fi SSID/password
- MQTT broker host/port
- robot ID

in:

- `firmware/esp32_uart0_phase1_bridge/config.example.h` -> local `config.h`

### 4. Run teleop

```powershell
.\.venv\Scripts\python.exe .\tools\mqtt_teleop.py --broker <broker-ip> --robot-id car-01
```

Controls:

- `w` forward
- `s` backward
- `a` left
- `d` right
- `x` stop
- `q` quit

## Important notes

- The ATmega runtime uses its hardware serial path, so USB serial monitoring on the Nano can interfere with board-to-board runtime communication.
- For first motion tests, keep the wheels off the ground.
- The final working Phase 1 route is documented in `doc/phase1_uart0_plan.md`.

## Key references

- `doc/current_findings.md`
- `doc/phase1_uart0_plan.md`
- `doc/esp32WG.ino`
- `doc/uploader.html`
- `doc/Tai_finder_X1/`
