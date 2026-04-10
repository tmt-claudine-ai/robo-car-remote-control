import argparse
import json
import math
import signal
import socket
import sys
import time
from dataclasses import dataclass

import paho.mqtt.client as mqtt


@dataclass
class CarState:
    owner_id: str = ""
    owner_expires_at: float = 0.0
    left: int = 0
    right: int = 0
    x: float = 0.0
    y: float = 0.0
    heading_deg: float = 0.0
    runtime_state: str = "IDLE"
    last_command_at: float = 0.0
    command_ttl_ms: int = 250
    last_seq: int = 0

    def owner_active(self) -> bool:
        return bool(self.owner_id) and time.monotonic() < self.owner_expires_at

    def clear_motion(self) -> None:
        self.left = 0
        self.right = 0
        self.runtime_state = "IDLE"


def clamp(value: int, low: int, high: int) -> int:
    return max(low, min(high, value))


class VirtualCar:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.state = CarState()
        self.started_at = time.monotonic()
        self.hostname = socket.gethostname()
        self.running = True

        base_topic = f"robot/{args.robot_id}"
        self.claim_topic = f"{base_topic}/session/claim"
        self.release_topic = f"{base_topic}/session/release"
        self.cmd_topic = f"{base_topic}/cmd"
        self.stop_topic = f"{base_topic}/cmd/stop"
        self.telemetry_topic = f"{base_topic}/telemetry"
        self.status_topic = f"{base_topic}/status"

        client_id = f"virtual-car-{args.robot_id}"
        self.client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
        if args.username:
            self.client.username_pw_set(args.username, args.password)
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message

    def publish_status(self, state: str, detail: str) -> None:
        payload = {
            "robotId": self.args.robot_id,
            "mode": "virtual",
            "state": state,
            "detail": detail,
            "owner": self.state.owner_id if self.state.owner_active() else "",
            "host": self.hostname,
            "mqttConnected": True,
        }
        self.client.publish(self.status_topic, json.dumps(payload), qos=0, retain=True)

    def publish_telemetry(self) -> None:
        payload = {
            "robotId": self.args.robot_id,
            "mode": "virtual",
            "uptimeMs": int((time.monotonic() - self.started_at) * 1000),
            "left": self.state.left,
            "right": self.state.right,
            "runtimeState": self.state.runtime_state,
            "x": round(self.state.x, 3),
            "y": round(self.state.y, 3),
            "headingDeg": round(self.state.heading_deg, 2),
            "owner": self.state.owner_id if self.state.owner_active() else "",
            "seq": self.state.last_seq,
        }
        self.client.publish(self.telemetry_topic, json.dumps(payload), qos=0, retain=False)

    def on_connect(self, client, _userdata, _flags, reason_code, _properties) -> None:
        print(f"connected: reason={reason_code}")
        client.subscribe(
            [
                (self.claim_topic, 0),
                (self.release_topic, 0),
                (self.cmd_topic, 0),
                (self.stop_topic, 0),
            ]
        )
        self.publish_status("online", "virtual_car_ready")

    def on_message(self, _client, _userdata, msg) -> None:
        payload = msg.payload.decode(errors="replace").strip()
        print(f"[{msg.topic}] {payload}")

        if msg.topic == self.claim_topic:
            self.handle_claim(payload)
            return
        if msg.topic == self.release_topic:
            self.handle_release(payload)
            return
        if msg.topic == self.cmd_topic:
            self.handle_cmd(payload)
            return
        if msg.topic == self.stop_topic:
            self.handle_stop(payload)

    def handle_claim(self, payload: str) -> None:
        parts = payload.split(",")
        if len(parts) != 2:
            self.publish_status("error", "bad_claim_payload")
            return

        controller_id = parts[0].strip()
        try:
            lease_ms = clamp(int(parts[1]), 1000, 10000)
        except ValueError:
            self.publish_status("error", "bad_claim_payload")
            return

        if not self.state.owner_active() or self.state.owner_id == controller_id:
            self.state.owner_id = controller_id
            self.state.owner_expires_at = time.monotonic() + lease_ms / 1000.0
            self.publish_status("claimed", "owner_updated")
        else:
            self.publish_status("busy", "already_claimed")

    def handle_release(self, payload: str) -> None:
        controller_id = payload.strip()
        if self.state.owner_active() and self.state.owner_id == controller_id:
            self.state.clear_motion()
            self.state.owner_id = ""
            self.state.owner_expires_at = 0.0
            self.publish_status("idle", "released")

    def handle_stop(self, payload: str) -> None:
        controller_id = payload.strip()
        if self.state.owner_active() and self.state.owner_id == controller_id:
            self.state.clear_motion()
            self.publish_status("idle", "stop")

    def handle_cmd(self, payload: str) -> None:
        parts = payload.split(",")
        if len(parts) != 5:
            self.publish_status("error", "bad_cmd_payload")
            return

        controller_id = parts[0].strip()
        if not self.state.owner_active() or self.state.owner_id != controller_id:
            self.publish_status("busy", "owner_mismatch")
            return

        try:
            seq = int(parts[1])
            left = clamp(int(parts[2]), -255, 255)
            right = clamp(int(parts[3]), -255, 255)
            ttl_ms = clamp(int(parts[4]), 120, 1000)
        except ValueError:
            self.publish_status("error", "bad_cmd_payload")
            return

        self.state.last_seq = seq
        self.state.left = left
        self.state.right = right
        self.state.last_command_at = time.monotonic()
        self.state.command_ttl_ms = ttl_ms
        self.state.owner_expires_at = time.monotonic() + self.args.lease_ms / 1000.0
        self.state.runtime_state = "ACTIVE" if left or right else "IDLE"

    def tick(self, dt: float) -> None:
        now = time.monotonic()

        if self.state.owner_id and not self.state.owner_active():
            self.state.owner_id = ""
            self.state.owner_expires_at = 0.0
            self.state.clear_motion()
            self.publish_status("idle", "lease_expired")

        if (self.state.left or self.state.right) and now - self.state.last_command_at > self.state.command_ttl_ms / 1000.0:
            self.state.clear_motion()
            self.state.runtime_state = "TIMEOUT"
            self.publish_status("idle", "cmd_timeout")

        linear = (self.state.left + self.state.right) / 510.0 * self.args.max_linear_speed
        angular = (self.state.right - self.state.left) / 510.0 * self.args.max_turn_rate_deg

        self.state.heading_deg += angular * dt
        heading_rad = math.radians(self.state.heading_deg)
        self.state.x += math.cos(heading_rad) * linear * dt
        self.state.y += math.sin(heading_rad) * linear * dt

    def run(self) -> int:
        self.client.connect(self.args.broker, self.args.port, keepalive=20)
        self.client.loop_start()

        signal.signal(signal.SIGINT, self.handle_signal)
        if hasattr(signal, "SIGTERM"):
            signal.signal(signal.SIGTERM, self.handle_signal)

        last_tick = time.monotonic()
        last_telemetry = 0.0
        try:
            while self.running:
                now = time.monotonic()
                dt = now - last_tick
                last_tick = now
                self.tick(dt)

                if now - last_telemetry >= self.args.telemetry_ms / 1000.0:
                    last_telemetry = now
                    self.publish_telemetry()

                time.sleep(0.05)
        finally:
            self.publish_status("offline", "virtual_car_exit")
            time.sleep(0.1)
            self.client.loop_stop()
            self.client.disconnect()
        return 0

    def handle_signal(self, _signum, _frame) -> None:
        self.running = False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Virtual MQTT robot for stage-1 local testing")
    parser.add_argument("--broker", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--robot-id", default="car-01")
    parser.add_argument("--lease-ms", type=int, default=3000)
    parser.add_argument("--telemetry-ms", type=int, default=250)
    parser.add_argument("--max-linear-speed", type=float, default=1.0, help="virtual units per second")
    parser.add_argument("--max-turn-rate-deg", type=float, default=180.0, help="degrees per second")
    return parser.parse_args()


def main() -> int:
    car = VirtualCar(parse_args())
    return car.run()


if __name__ == "__main__":
    raise SystemExit(main())
