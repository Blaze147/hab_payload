"""Decode balloon HABT-v1 payloads delivered by the ground MessageManager."""
import struct
from message_codec import decode_sensors


def parse_ground_line(line):
    parts = line.split()
    if not parts or parts[0] != 'GROUND_RX':
        return None
    fields = dict(part.split('=', 1) for part in parts[1:] if '=' in part)
    try:
        # Use the exact received bytes, not the optional human-readable suffix.
        raw = bytes.fromhex(fields['hex'])
        session, message = int(fields['session']), int(fields['message'])
        if raw[:1] == b'\xa1':
            if int(fields['bytes']) != len(raw) or not 0 <= session <= 65535 or not 0 <= message <= 65535:
                return None
            result = decode_sensors(raw)
            result['telemetry'].update(session=session, message=message)
            return result
        if (int(fields['bytes']) != 32 or len(raw) != 32 or raw[:5] != b'HABT\x01'
                or raw[5] & ~0x1f or raw[6:8] != b'\0\0'
                or not 0 <= session <= 65535 or not 0 <= message <= 65535):
            return None
        uptime, lat, lon, altitude, temperature, humidity, satellites = struct.unpack('>IiiihHI', raw[8:])
        flags = raw[5]
        if flags & 2 and not (-90000000 <= lat <= 90000000 and -180000000 <= lon <= 180000000):
            return None
        if flags & 16 and not (-400 <= temperature <= 800 and humidity <= 1000):
            return None
    except (KeyError, ValueError, struct.error):
        return None
    sample = {
        'status': 'FIX' if flags & 2 else 'NO_FIX' if flags & 1 else 'NO_DATA',
        'lat': lat / 1e6 if flags & 2 else None,
        'lon': lon / 1e6 if flags & 2 else None,
        'alt_m': altitude / 10 if flags & 4 else None,
        'sats': satellites if flags & 8 else None,
        'speed_kmh': None, 'pps': None, 'utc': None,
    }
    environment = {
        'status': 'OK' if flags & 16 else 'UNAVAILABLE',
        'c': temperature / 10 if flags & 16 else None,
        'humidity_pct': humidity / 10 if flags & 16 else None,
    }
    return {
        'sample': sample, 'environment': environment,
        'telemetry': {'session': session, 'message': message,
                      'balloon_uptime_ms': uptime, 'flags': flags, 'payload_hex': raw.hex()},
    }
