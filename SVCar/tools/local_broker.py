import argparse
import asyncio
import signal

from amqtt.broker import Broker


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Local MQTT broker for repo testing")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=1883)
    return parser.parse_args()


async def run_broker(host: str, port: int) -> None:
    config = {
        "listeners": {
            "default": {
                "type": "tcp",
                "bind": f"{host}:{port}",
            }
        },
        "plugins": {
            "amqtt.plugins.authentication.AnonymousAuthPlugin": {
                "allow_anonymous": True
            }
        },
    }

    broker = Broker(config)
    await broker.start()
    stop_event = asyncio.Event()

    loop = asyncio.get_running_loop()
    for sig_name in ("SIGINT", "SIGTERM"):
        sig = getattr(signal, sig_name, None)
        if sig is not None:
            try:
                loop.add_signal_handler(sig, stop_event.set)
            except NotImplementedError:
                pass

    print(f"local broker listening on mqtt://{host}:{port}", flush=True)
    try:
        await stop_event.wait()
    finally:
        await broker.shutdown()


def main() -> int:
    args = parse_args()
    try:
        asyncio.run(run_broker(args.host, args.port))
    except KeyboardInterrupt:
        pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
