# Migration Manifest

Target repo:

- `../robo-car-remote-control/SVCar`

Maintained deliverables:

- `firmware/esp32_uart0_phase1_bridge/`
  - Final ESP32-S3 MQTT-to-UART bridge used for the working Phase 1 path
- `firmware/nano_phase1_uart0_runtime/`
  - Final ATmega328P runtime used for the working Phase 1 path
- `tools/mqtt_teleop.py`
  - Keyboard teleoperation client
- `tools/local_broker.py`
  - Local MQTT broker runner used for development
- `tools/virtual_car.py`
  - Virtual robot used for protocol and broker testing
- `tools/requirements.txt`
  - Python dependencies for the local toolchain
- `scripts/setup_env.ps1`
  - Python environment bootstrap
- `scripts/start_local_broker.ps1`
  - Local broker startup helper
- `scripts/stop_local_broker.ps1`
  - Local broker shutdown helper
- `doc/current_findings.md`
  - Current technical conclusion after the real-car bring-up
- `doc/phase1_uart0_plan.md`
  - Working bring-up plan for the UART0-based Phase 1 path
- `doc/esp32WG.ino`
  - Reference gateway/uploader sketch that revealed the actual board link
- `doc/uploader.html`
  - Reference uploader frontend paired with the gateway sketch
- `doc/Tai_finder_X1/`
  - Reference motherboard library used to understand the real hardware stack

Intentionally not migrated:

- `firmware/atmega_link_probe/`
- `firmware/esp32_link_probe/`
- `firmware/atmega_remote_runtime/`
- `firmware/esp32_mqtt_bridge/`
- `doc/link_probe.md`
- `doc/stage1_alternative_plan.md`
- `doc/stage1_mvp.md`
- `doc/mixly_xml/`
- `tools/__pycache__/`

Rationale:

- The migrated set keeps the final working Phase 1 implementation, the minimum local tooling required to operate it, and the hardware references that were actually useful in reaching the working design.
- Early experiments, superseded drafts, probe utilities, and large reference dumps that are not needed to operate the final stack were left behind.
