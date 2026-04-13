#!/usr/bin/env bash
cd /Users/ericc/.openclaw/workspace/demos/robo-car-remote-control/SVCar
exec python3 tools/virtual_car.py --broker localhost --username car-01 --password 'bb-car01-2026' --robot-id car-01
