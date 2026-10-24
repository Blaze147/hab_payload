import struct
import unittest
from message_codec import decode_sensors
from telemetry import parse_ground_line
import server


class MessageCodecTests(unittest.TestCase):
    golden = bytes.fromhex('a10000007b01020a0a02020fdc0402f3ef')

    def line(self, raw):
        return f'GROUND_RX session=77 message=12 bytes={len(raw)} hex={raw.hex()}'

    def test_cpp_golden_vector(self):
        value = parse_ground_line(self.line(self.golden))
        self.assertEqual(value['environment']['c'], 25.7)
        self.assertEqual(value['environment']['humidity_pct'], 40.6)
        self.assertEqual(value['sensors']['external_rtd_c'], -30.89)
        self.assertIsNone(value['sensors']['internal_low_temperature_c'])
        self.assertEqual(value['telemetry']['balloon_uptime_ms'], 123)

    def test_all_sensor_identities(self):
        raw = (self.golden + b'\x03\x02' + struct.pack('>h', -4000)
               + b'\x05\x04' + struct.pack('>I', 1000)
               + b'\x06\x0f' + struct.pack('>BiiiH', 15, 0, -180000000, -500, 20))
        value = parse_ground_line(self.line(raw))
        self.assertEqual(value['sensors']['internal_low_temperature_c'], -40)
        self.assertEqual(value['sensors']['external_rtd_c'], -30.89)
        self.assertEqual(value['sensors']['pressure_pa'], 1000)
        self.assertEqual(value['sample']['lat'], 0)
        self.assertEqual(value['sample']['lon'], -180)
        self.assertEqual(value['sample']['alt_m'], -50)

    def test_ds18b20_range_and_identity(self):
        for centi in (-5500, -4000, -2000, 0, 12500):
            raw = bytes.fromhex('a1000000010302') + struct.pack('>h', centi)
            value = parse_ground_line(self.line(raw))
            self.assertEqual(value['sensors']['internal_low_temperature_c'], centi / 100)
            self.assertIsNone(value['sensors']['external_rtd_c'])
        for centi in (-5501, 12501):
            raw = bytes.fromhex('a1000000010302') + struct.pack('>h', centi)
            self.assertIsNone(parse_ground_line(self.line(raw)))

    def test_full_outbound_sensor_packet_cpp_golden(self):
        # Captured from the native MessageManager -> ProtocolManager -> RF test.
        raw = bytes.fromhex(
            'a10000007b01020abe02020bea03020898040208fc05040001892d'
            '060f0f025ab8e0f9bdd3c000004268000c')
        self.assertEqual(len(raw), 44)
        value = parse_ground_line(self.line(raw))
        self.assertEqual(value['environment']['c'], 27.5)
        self.assertEqual(value['environment']['humidity_pct'], 30.5)
        self.assertEqual(value['sensors']['internal_low_temperature_c'], 22)
        self.assertEqual(value['sensors']['external_rtd_c'], 23)
        self.assertEqual(value['sensors']['pressure_pa'], 100653)
        self.assertEqual(value['sample']['lat'], 39.5)
        self.assertEqual(value['sample']['lon'], -105)
        self.assertEqual(value['sample']['alt_m'], 1700)
        self.assertEqual(value['sample']['sats'], 12)

    def test_absent_sensors_are_unavailable(self):
        value = decode_sensors(bytes.fromhex('a100000000'))
        self.assertTrue(all(v is None for v in value['sensors'].values()))
        self.assertEqual(value['environment']['status'], 'UNAVAILABLE')

    def test_rejects_malformed_and_duplicate_records(self):
        for raw in (self.golden[:-1], self.golden + self.golden[5:9],
                    self.golden + b'\x07\x00', self.golden + b'\x42\x00',
                    b'\xa1\0\0\0\0\1\xff', b'\xa1\0\0\0\0\2\2\xff\xff'):
            self.assertIsNone(parse_ground_line(self.line(raw)))

    def test_ground_bridge_exposes_external_rtd_without_relabeling_dht(self):
        saved = dict(server.state)
        try:
            server.state.update(source='ground', received_messages=0)
            server.consume_line(self.line(self.golden))
            value = server.snapshot()
            self.assertEqual(value['sensors']['external_rtd_c'], -30.89)
            self.assertEqual(value['environment']['c'], 25.7)
        finally:
            server.state.clear()
            server.state.update(saved)


if __name__ == '__main__':
    unittest.main()
