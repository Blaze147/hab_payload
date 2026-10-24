#!/usr/bin/env python3
"""Local balloon/ground telemetry dashboard bridge."""
import argparse
import json
import math
import threading
import time
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import serial
from serial.tools import list_ports
from camera import CameraReceiver
from telemetry import parse_ground_line
from activity import ActivityLog

lock = threading.Lock()
camera = CameraReceiver()
activity = ActivityLog()
serial_log = None
state = {"connected": False, "port": None, "sample": None,
         "received_at": None, "environment": None, "environment_received_at": None,
         "source": "auto", "telemetry": None, "received_messages": 0,
         "environment_latest_status": None,
         "error": "Connecting to RP2040…"}


def parse_environment(line):
    parts = line.split()
    if len(parts) < 2 or parts[0] != "ENV":
        return None
    data = {"status": parts[1]}
    for part in parts[2:]:
        key, sep, value = part.partition("=")
        if sep:
            data[key] = value
    for key in ("c", "humidity_pct", "sequence"):
        try:
            value = float(data.get(key, "NA"))
            data[key] = value if math.isfinite(value) else None
        except ValueError:
            data[key] = None
    if data['c'] is not None and not -40 <= data['c'] <= 80:
        data['c'] = None
    if data['humidity_pct'] is not None and not 0 <= data['humidity_pct'] <= 100:
        data['humidity_pct'] = None
    if data['status'] != 'OK':
        data['c'] = data['humidity_pct'] = None
    return data


def parse_line(line):
    parts = line.split()
    if len(parts) < 2 or parts[0] != "GPS":
        return None
    data = {"status": parts[1]}
    for part in parts[2:]:
        key, sep, value = part.partition("=")
        if sep:
            data[key] = value
    for key in ("lat", "lon", "alt_m", "speed_kmh", "sats", "pps", "pps_age_ms"):
        try:
            value = float(data.get(key, "NA"))
            data[key] = value if math.isfinite(value) else None
        except ValueError:
            data[key] = None
    if data["lat"] is not None and not -90 <= data["lat"] <= 90:
        data["lat"] = None
    if data["lon"] is not None and not -180 <= data["lon"] <= 180:
        data["lon"] = None
    return data


def update_environment(environment, now):
    """Keep the last good measurement and its timestamp across failed updates."""
    valid = (environment['status'] == 'OK'
             and all(isinstance(environment.get(key), (int, float))
                     and math.isfinite(environment[key]) for key in ('c', 'humidity_pct')))
    state['environment_latest_status'] = environment['status'] if not valid else 'OK'
    if valid:
        state.update(environment=environment, environment_received_at=now)
    elif state['environment'] is None:
        state['environment'] = environment


def consume_line(line):
    ground = parse_ground_line(line)
    with lock:
        if line.startswith('GROUND_EVENT '):
            if state['source'] != 'balloon':
                activity.feed(line)
            return
        if line.startswith('SPLIT_HOST '):
            state['radio'] = dict(p.split('=', 1) for p in line.split()[1:] if '=' in p)
        if ground and state['source'] != 'balloon':
            now = time.time()
            environment = ground.pop('environment')
            state.update(source='ground', **ground, received_at=now,
                         received_messages=state['received_messages'] + 1)
            update_environment(environment, now)
            return
        if line.startswith('GROUND_RX ') and state['source'] != 'balloon':
            state['environment_latest_status'] = 'BAD_PACKET'
        if state['source'] == 'ground':
            return
        camera.feed(line)
        sample = parse_line(line)
        if sample:
            state.update(source='balloon', sample=sample, received_at=time.time())
        environment = parse_environment(line)
        if environment:
            update_environment(environment, time.time())


def snapshot():
    with lock:
        payload = dict(state)
        payload['camera'] = (None if state['source'] == 'ground' else camera.snapshot())
        payload['activity'] = activity.snapshot()
    now = time.time()
    payload['age_seconds'] = now - payload['received_at'] if payload['received_at'] is not None else None
    payload['environment_age_seconds'] = (
        now - payload['environment_received_at'] if payload['environment_received_at'] is not None else None)
    # P0 packet airtime plus scheduled beacon/control windows create legitimate
    # pauses. Local direct-USB sensor feeds retain their tighter age limits.
    payload['freshness_seconds'] = {'gps': 60, 'environment': 60} if payload['source'] == 'ground' else {'gps': 4, 'environment': 6}
    return payload


def read_device(requested_port, requested_serial=None):
    while True:
        try:
            port = requested_port
            if not port:
                port = next((p.device for p in list_ports.comports()
                             if p.vid == 0x2E8A and p.pid == 0x0003
                             and (not requested_serial or p.serial_number == requested_serial)), None)
            if not port:
                raise serial.SerialException("RP2040 not found. Connect its USB cable.")
            with serial.Serial(port, 115200, timeout=1, exclusive=True) as device:
                with lock:
                    camera.reset_transfer()
                    state.update(connected=True, port=port, error=None)
                while True:
                    line = device.read_until(b'\n', 1024).decode("ascii", errors="replace")
                    if serial_log is not None:
                        serial_log.write(line);serial_log.flush()
                    consume_line(line)
        except (serial.SerialException, OSError) as error:
            with lock:
                state.update(connected=False, error=str(error))
            time.sleep(2)


class Handler(SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.split('?', 1)[0] == "/camera.jpg":
            with lock:
                image = None if state['source'] == 'ground' else camera.jpeg
            if image is None:
                self.send_error(404, "No verified camera image yet")
                return
            self.send_response(200)
            self.send_header("Content-Type", "image/jpeg")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(image)))
            self.end_headers()
            self.wfile.write(image)
        elif self.path == "/api/gps":
            payload = snapshot()
            body = json.dumps(payload, allow_nan=False).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        else:
            super().do_GET()

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial-port", help="Defaults to automatic RP2040 USB detection")
    parser.add_argument("--serial-number", help="Follow a specific RP2040 across USB port changes")
    parser.add_argument("--source", choices=['auto', 'ground', 'balloon'], default='auto',
                        help="ground reads received RF telemetry; balloon reads local USB sensors")
    parser.add_argument("--serial-log", help="Optional raw serial capture file for diagnostics")
    parser.add_argument("--port", type=int, default=8765, help="Local web port")
    args = parser.parse_args()
    state['source'] = args.source
    if args.serial_log:
        serial_log = open(args.serial_log, 'a', buffering=1)
    server = ThreadingHTTPServer(("127.0.0.1", args.port), partial(
        Handler, directory=str(Path(__file__).parent / "static")))
    threading.Thread(target=read_device, args=(args.serial_port, args.serial_number), daemon=True).start()
    print(f"Telemetry dashboard running at http://127.0.0.1:{args.port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.server_close()
