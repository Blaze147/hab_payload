import struct
import unittest
from unittest.mock import patch

import server
from telemetry import parse_ground_line


def record(flags=31, lat=39563668, lon=-105016780, temperature=257, humidity=406):
    raw = b'HABT\x01' + bytes([flags, 0, 0]) + struct.pack(
        '>IiiihHI', 3048367, lat, lon, 17175, temperature, humidity, 11)
    return f'GROUND_RX ms=129374 session=7405 message=305 bytes=32 hex={raw.hex()}'


class TelemetryTests(unittest.TestCase):
    def setUp(self):
        self.saved_state = dict(server.state)

    def tearDown(self):
        server.state.clear()
        server.state.update(self.saved_state)

    def test_real_balloon_payload(self):
        value = parse_ground_line(record())
        self.assertEqual(value['sample']['status'], 'FIX')
        self.assertAlmostEqual(value['sample']['lat'], 39.563668)
        self.assertAlmostEqual(value['sample']['lon'], -105.016780)
        self.assertEqual(value['sample']['alt_m'], 1717.5)
        self.assertEqual(value['sample']['sats'], 11)
        self.assertEqual(value['environment']['c'], 25.7)
        self.assertEqual(value['environment']['humidity_pct'], 40.6)
        self.assertEqual(value['telemetry']['balloon_uptime_ms'], 3048367)

    def test_signed_temperature_and_zero_coordinates(self):
        value = parse_ground_line(record(lat=0, lon=0, temperature=-123))
        self.assertEqual(value['environment']['c'], -12.3)
        self.assertEqual(value['sample']['lat'], 0)
        self.assertEqual(value['sample']['lon'], 0)

    def test_invalid_sensor_flags_retain_last_good_environment(self):
        server.state.update(source='ground', received_messages=0)
        server.consume_line(record())
        server.consume_line(record(flags=0))
        value = server.snapshot()
        self.assertIsNone(value['sample']['lat'])
        self.assertIsNone(value['sample']['alt_m'])
        self.assertIsNone(value['sample']['sats'])
        self.assertEqual(value['environment']['c'], 25.7)
        self.assertEqual(value['environment']['humidity_pct'], 40.6)
        self.assertEqual(value['environment_latest_status'], 'UNAVAILABLE')
        self.assertEqual(value['received_messages'], 2)

    def test_bad_updates_age_from_last_good_reading_and_recover(self):
        server.state.update(source='ground', environment=None, environment_received_at=None)
        with patch('server.time.time', return_value=100):
            server.consume_line(record())
        with patch('server.time.time', return_value=102):
            server.consume_line(record(flags=15))
            self.assertEqual(server.snapshot()['environment_age_seconds'], 2)
        with patch('server.time.time', return_value=180):
            server.consume_line(record(humidity=1001))
            value = server.snapshot()
        self.assertEqual(value['environment_age_seconds'], 80)
        self.assertEqual(value['environment']['humidity_pct'], 40.6)
        self.assertEqual(value['environment_latest_status'], 'BAD_PACKET')
        with patch('server.time.time', return_value=181):
            server.consume_line(record(temperature=200, humidity=500))
            value = server.snapshot()
        self.assertEqual(value['environment_age_seconds'], 0)
        self.assertEqual(value['environment']['c'], 20)
        self.assertEqual(value['environment_latest_status'], 'OK')

    def test_direct_sensor_failure_retains_values_and_timestamp(self):
        server.state.update(source='balloon', environment=None, environment_received_at=None)
        with patch('server.time.time', return_value=100):
            server.consume_line('ENV OK c=20 humidity_pct=50 sequence=1')
        with patch('server.time.time', return_value=102):
            server.consume_line('ENV READ_ERROR c=NA humidity_pct=NA sequence=2')
            value = server.snapshot()
        self.assertEqual(value['environment']['c'], 20)
        self.assertEqual(value['environment_age_seconds'], 2)
        self.assertEqual(value['environment_latest_status'], 'READ_ERROR')

    def test_bad_payload_is_not_published(self):
        valid = record()
        for line in (valid.replace('bytes=32', 'bytes=31'),
                     valid.replace('4841425401', '4841425402'),
                     valid.replace('hex=4841', 'hex=xx41'),
                     valid.replace('session=7405', 'session=65536'),
                     record(lat=90000001), record(humidity=1001),
                     record(flags=128), valid[:-2], 'GPS FIX lat=1 lon=2'):
            with self.subTest(line=line):
                self.assertIsNone(parse_ground_line(line))

    def test_decode_uses_wire_bytes_not_diagnostic_suffix(self):
        value = parse_ground_line(record() + ' temperatureTenthsC=999 latE6=1')
        self.assertEqual(value['environment']['c'], 25.7)
        self.assertEqual(value['sample']['lat'], 39.563668)

    def test_ground_ignores_local_sensor_and_camera_lines(self):
        server.state.update(source='ground', sample=None, environment=None)
        server.consume_line('GPS FIX lat=1 lon=2')
        server.consume_line('ENV OK c=99 humidity_pct=99')
        server.consume_line('CAM_STATUS CAPTURED')
        self.assertIsNone(server.state['sample'])
        self.assertIsNone(server.state['environment'])
        self.assertIsNone(server.snapshot()['camera'])

    def test_age_tracks_rf_messages_not_usb_status(self):
        server.state.update(source='ground', received_messages=0)
        with patch('server.time.time', return_value=100):
            server.consume_line(record())
        with patch('server.time.time', return_value=175):
            server.consume_line('SPLIT_HOST connected=1 established=1 dataProfile=P0')
            value = server.snapshot()
        self.assertEqual(value['age_seconds'], 75)
        self.assertEqual(value['environment_age_seconds'], 75)
        self.assertEqual(value['freshness_seconds']['gps'], 60)
        self.assertEqual(value['radio']['dataProfile'], 'P0')

    def test_balloon_direct_usb_still_works(self):
        server.state.update(source='balloon', sample=None, environment=None)
        server.consume_line('GPS FIX lat=1 lon=-2 alt_m=-10 sats=8')
        server.consume_line('ENV OK c=20 humidity_pct=50 sequence=1')
        self.assertEqual(server.state['sample']['lon'], -2)
        self.assertEqual(server.state['environment']['humidity_pct'], 50)
        self.assertEqual(server.snapshot()['freshness_seconds']['gps'], 4)


if __name__ == '__main__':
    unittest.main()
