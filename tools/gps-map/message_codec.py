"""Typed MessageManager A1 records. Mirrors lib/MessageManager/src/MessageCodec.h."""
import struct


def decode_sensors(raw):
    if len(raw) < 5 or raw[0] != 0xA1:
        raise ValueError('unsupported message version')
    uptime, = struct.unpack_from('>I', raw, 1)
    at, values = 5, {}
    while at < len(raw):
        if len(raw) - at < 2:
            raise ValueError('truncated record')
        kind, length = raw[at:at + 2]
        at += 2
        if length == 255:
            if len(raw) - at < 2:
                raise ValueError('truncated extended length')
            length, = struct.unpack_from('>H', raw, at)
            at += 2
        if kind in values or kind not in range(1, 7) or length > len(raw) - at:
            raise ValueError('invalid sensor record')
        value = raw[at:at + length]
        at += length
        expected = {1: 2, 2: 2, 3: 2, 4: 2, 5: 4, 6: 15}[kind]
        if length != expected:
            raise ValueError('invalid record length')
        if kind in (1, 3, 4):
            decoded, = struct.unpack('>h', value)
            minimum, maximum = {1: (-4000, 8000), 3: (-5500, 12500), 4: (-20000, 32767)}[kind]
            if not minimum <= decoded <= maximum:
                raise ValueError('temperature out of range')
            decoded /= 100
        elif kind == 2:
            decoded, = struct.unpack('>H', value)
            if decoded > 10000:
                raise ValueError('humidity out of range')
            decoded /= 100
        elif kind == 5:
            decoded, = struct.unpack('>I', value)
            if not 0 < decoded <= 200000:
                raise ValueError('pressure out of range')
        else:
            flags, lat, lon, altitude, satellites = struct.unpack('>BiiiH', value)
            if flags & ~15 or (flags & 2 and not (-90000000 <= lat <= 90000000 and -180000000 <= lon <= 180000000)):
                raise ValueError('invalid GPS')
            decoded = {'flags': flags, 'lat': lat / 1e6 if flags & 2 else None,
                       'lon': lon / 1e6 if flags & 2 else None,
                       'alt_m': altitude / 10 if flags & 4 else None,
                       'sats': satellites if flags & 8 else None}
        values[kind] = decoded
    gps = values.get(6, {'flags': 0, 'lat': None, 'lon': None, 'alt_m': None, 'sats': None})
    flags = gps.pop('flags')
    gps.update(status='FIX' if flags & 2 else 'NO_FIX' if flags & 1 else 'NO_DATA',
               speed_kmh=None, pps=None, utc=None)
    environment = {'status': 'OK' if 1 in values and 2 in values else 'UNAVAILABLE',
                   'c': values.get(1), 'humidity_pct': values.get(2)}
    sensors = {'internal_temperature_c': values.get(1), 'internal_humidity_pct': values.get(2),
               'internal_low_temperature_c': values.get(3), 'external_rtd_c': values.get(4),
               'pressure_pa': values.get(5)}
    return {'sample': gps, 'environment': environment, 'sensors': sensors,
            'telemetry': {'balloon_uptime_ms': uptime, 'schema': 'A1', 'payload_hex': raw.hex()}}
