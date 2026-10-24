import base64
import binascii
import io
import unittest

from PIL import Image
from camera import CameraReceiver


class CameraTests(unittest.TestCase):
    def setUp(self):
        stream = io.BytesIO()
        Image.new('RGB', (320, 240), '#245568').save(stream, format='JPEG')
        self.jpeg = stream.getvalue()
        self.receiver = CameraReceiver()

    def begin(self, crc=None):
        if crc is None:
            crc = binascii.crc32(self.jpeg)
        self.receiver.feed(f'CAM_BEGIN id=1 bytes={len(self.jpeg)} crc={crc:08X} width=320 height=240')

    def send(self):
        for offset in range(0, len(self.jpeg), 48):
            encoded = base64.b64encode(self.jpeg[offset:offset+48]).decode()
            self.receiver.feed('GPS FIX lat=1 lon=2')
            self.receiver.feed(f'CAM_DATA id=1 offset={offset} data={encoded}')
        self.receiver.feed('CAM_END id=1')

    def test_interleaved_frame_decodes(self):
        self.begin()
        self.send()
        self.assertEqual(self.receiver.jpeg, self.jpeg)
        self.assertEqual(self.receiver.snapshot()['image_id'], 1)

    def test_corrupt_frame_preserves_last_verified_image(self):
        self.begin()
        self.send()
        self.begin(crc=0)
        self.send()
        self.assertEqual(self.receiver.jpeg, self.jpeg)
        self.assertEqual(self.receiver.info['status'], 'INVALID_FRAME')

    def test_missing_chunk_rejected(self):
        self.begin()
        self.receiver.feed('CAM_DATA id=1 offset=48 data=YWJj')
        self.assertIsNone(self.receiver.jpeg)
        self.assertIsNone(self.receiver.pending)

    def test_oversized_and_expired_transfers_rejected(self):
        self.receiver.feed('CAM_BEGIN id=1 bytes=999999 crc=00000000')
        self.assertIsNone(self.receiver.pending)
        self.begin()
        self.receiver.pending['started'] -= 21
        self.receiver.feed('GPS FIX lat=1 lon=2')
        self.assertEqual(self.receiver.info['status'], 'TRANSFER_TIMEOUT')


if __name__ == '__main__':
    unittest.main()
