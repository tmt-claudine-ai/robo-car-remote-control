import argparse
import atexit
import os
import socket
import sys
import time

import paho.mqtt.client as mqtt

if os.name == "nt":
    import msvcrt
else:
    import select
    import termios
    import tty


def default_controller_id() -> str:
    return f"{socket.gethostname()}-{os.getpid()}"


class KeyReader:
    def __init__(self) -> None:
        self._old_settings = None

    def __enter__(self):
        if os.name != "nt":
            fd = sys.stdin.fileno()
            self._old_settings = termios.tcgetattr(fd)
            tty.setcbreak(fd)
        return self

    def __exit__(self, exc_type, exc, tb):
        if os.name != "nt" and self._old_settings is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, self._old_settings)

    def read_key(self):
        if os.name == "nt":
            if msvcrt.kbhit():
                return msvcrt.getwch().lower()
            return None

        readable, _, _ = select.select([sys.stdin], [], [], 0)
        if readable:
            return sys.stdin.read(1).lower()
        return None


def publish_claim(client, topic, controller_id, lease_ms):
    client.publish(topic, f"{controller_id},{lease_ms}", qos=0, retain=False)


def publish_release(client, topic, controller_id):
    client.publish(topic, controller_id, qos=0, retain=False)


def publish_stop(client, topic, controller_id):
    client.publish(topic, controller_id, qos=0, retain=False)


def publish_cmd(client, topic, controller_id, seq, left, right, ttl_ms):
    payload = f"{controller_id},{seq},{left},{right},{ttl_ms}"
    client.publish(topic, payload, qos=0, retain=False)


def command_for_key(key: str, drive_speed: int, turn_speed: int):
    if key == "w":
        return drive_speed, drive_speed
    if key == "s":
        return -drive_speed, -drive_speed
    if key == "a":
        return -turn_speed, turn_speed
    if key == "d":
        return turn_speed, -turn_speed
    if key in {"x", " "}:
        return 0, 0
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="Minimal MQTT teleop for the stage-1 robot MVP")
    parser.add_argument("--broker", required=True)
    parser.add_argument("--port", type=int, default=1883)
    parser.add_argument("--username")
    parser.add_argument("--password")
    parser.add_argument("--robot-id", default="car-01")
    parser.add_argument("--controller-id", default=default_controller_id())
    parser.add_argument("--lease-ms", type=int, default=3000)
    parser.add_argument("--ttl-ms", type=int, default=250)
    parser.add_argument("--drive-speed", type=int, default=180)
    parser.add_argument("--turn-speed", type=int, default=160)
    parser.add_argument("--tick-ms", type=int, default=100)
    parser.add_argument("--input-hold-ms", type=int, default=350)
    args = parser.parse_args()

    base_topic = f"robot/{args.robot_id}"
    claim_topic = f"{base_topic}/session/claim"
    release_topic = f"{base_topic}/session/release"
    cmd_topic = f"{base_topic}/cmd"
    stop_topic = f"{base_topic}/cmd/stop"
    telemetry_topic = f"{base_topic}/telemetry"
    status_topic = f"{base_topic}/status"

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=f"teleop-{args.controller_id}")
    if args.username:
        client.username_pw_set(args.username, args.password)

    def on_connect(_client, _userdata, _flags, reason_code, _properties):
        print(f"connected: reason={reason_code}")
        _client.subscribe([(telemetry_topic, 0), (status_topic, 0)])

    def on_message(_client, _userdata, msg):
        print(f"[{msg.topic}] {msg.payload.decode(errors='replace')}")

    client.on_connect = on_connect
    client.on_message = on_message
    client.connect(args.broker, args.port, keepalive=20)
    client.loop_start()

    publish_claim(client, claim_topic, args.controller_id, args.lease_ms)
    atexit.register(publish_release, client, release_topic, args.controller_id)
    atexit.register(publish_stop, client, stop_topic, args.controller_id)

    print("controls: w=forward s=back a=left d=right x=stop q=quit")
    print(f"controller_id={args.controller_id}")

    seq = 1
    left = 0
    right = 0
    last_claim_at = 0.0
    last_motion_at = 0.0

    with KeyReader() as reader:
        while True:
            now = time.monotonic()
            if now - last_claim_at > args.lease_ms / 2000.0:
                publish_claim(client, claim_topic, args.controller_id, args.lease_ms)
                last_claim_at = now

            key = reader.read_key()
            if key:
                if key == "q":
                    break
                cmd = command_for_key(key, args.drive_speed, args.turn_speed)
                if cmd is not None:
                    left, right = cmd
                    last_motion_at = now

            if left != 0 or right != 0:
                if now - last_motion_at > args.input_hold_ms / 1000.0:
                    left = 0
                    right = 0

            publish_cmd(client, cmd_topic, args.controller_id, seq, left, right, args.ttl_ms)
            seq += 1
            time.sleep(args.tick_ms / 1000.0)

    publish_stop(client, stop_topic, args.controller_id)
    publish_release(client, release_topic, args.controller_id)
    time.sleep(0.2)
    client.loop_stop()
    client.disconnect()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
