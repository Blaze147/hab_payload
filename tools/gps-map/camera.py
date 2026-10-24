"""Bounded, checksummed JPEG assembly from interleaved serial text lines."""
import base64
import binascii
import io
import time

from PIL import Image


class CameraReceiver:
    MAX_BYTES = 32768

    def __init__(self):
        self.jpeg = None
        self.info = {"status": "WAITING", "image_id": None, "received_at": None}
        self.pending = None

    def reset_transfer(self):
        self.pending = None

    def fail(self, reason):
        self.pending = None
        self.info.update(status=reason)

    def feed(self, line):
        if self.pending and time.monotonic() - self.pending["started"] > 20:
            self.fail("TRANSFER_TIMEOUT")
        parts = line.split()
        if not parts or not parts[0].startswith("CAM_"):
            return
        fields = dict(part.split("=", 1) for part in parts[1:] if "=" in part)
        try:
            if parts[0] == "CAM_STATUS":
                self.info.update(status=parts[1], **fields)
            elif parts[0] == "CAM_BEGIN":
                size = int(fields["bytes"])
                if not 4 <= size <= self.MAX_BYTES:
                    raise ValueError("Invalid frame size")
                self.pending = {"id": int(fields["id"]), "size": size,
                                "crc": int(fields["crc"], 16), "data": bytearray(),
                                "started": time.monotonic()}
                self.info.update(status="RECEIVING")
            elif parts[0] == "CAM_DATA":
                p = self.pending
                if not p or int(fields["id"]) != p["id"] or int(fields["offset"]) != len(p["data"]):
                    raise ValueError("Missing or reordered image chunk")
                chunk = base64.b64decode(fields["data"], validate=True)
                if not 1 <= len(chunk) <= 48 or len(p["data"]) + len(chunk) > p["size"]:
                    raise ValueError("Image size overflow")
                p["data"].extend(chunk)
            elif parts[0] == "CAM_END":
                p = self.pending
                if not p or int(fields["id"]) != p["id"] or len(p["data"]) != p["size"]:
                    raise ValueError("Incomplete frame")
                data = bytes(p["data"])
                if binascii.crc32(data) != p["crc"] or not data.startswith(b'\xff\xd8') or not data.endswith(b'\xff\xd9'):
                    raise ValueError("Image checksum or marker failure")
                with Image.open(io.BytesIO(data)) as image:
                    if image.format != "JPEG" or image.size != (320, 240):
                        raise ValueError("Unexpected image format or dimensions")
                    image.load()  # Confirm the captured JPEG actually decodes.
                self.jpeg = data
                self.info.update(status="CAPTURED", image_id=p["id"], bytes=len(data),
                                 width=320, height=240, received_at=time.time())
                self.pending = None
        except (KeyError, ValueError, OSError, IndexError):
            self.fail("INVALID_FRAME")

    def snapshot(self):
        result = dict(self.info)
        result["age_seconds"] = (time.time() - result["received_at"]
                                 if result["received_at"] else None)
        return result
