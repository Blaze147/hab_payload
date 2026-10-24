# MessageManager: typed data and complete messages

The application passes typed values to MessageManager. MessageCodec (part of
that library) owns their byte representation. MessageManager owns transmit
identities, fragmentation, buffering, reassembly and dispatch. ProtocolManager
owns packet framing and CRC; LinkManager owns the data gate and window timing.
The split RP2040 uses RemoteProtocol as its PacketService; the same manager
works with a local ProtocolManager.

```text
Sensor drivers -> application selects fresh readings
               -> MessageManager.sendSensors(SensorData)
               -> shared MessageCodec -> complete message buffer
               -> MessageManager fragments -> PacketService -> LoRa
LoRa -> PacketService -> MessageManager reassembles and validates
     -> Receiver.onSensors(SensorData) / onCamera(CameraView)
```

## Application interface

Construct the manager with caller-owned `MessageManager::Storage` for outbound
messages or multi-fragment reception. The split host runtime allocates it once.
`begin(bootSession)` registers the packet callback, resets state and clears the
application receiver; register the receiver afterwards. The host supplies a
random 16-bit boot session. Message IDs increment automatically and session
advances when the message ID wraps.

```cpp
messaging::SensorData sample;
sample.uptimeMs = now;
sample.internalTemperature = true;
sample.internalTemperatureCentiC = 2570; // DHT: 25.70 C
sample.externalRtd = true;
sample.externalRtdCentiC = -3089; // corrected PT100: -30.89 C
messages.sendSensors(sample, now);
```

Booleans are presence/validity, not sensor readings. The application selects
fresh, valid samples and converts physical units into documented integers; it
never chooses wire offsets or constructs packet headers. A missing record means
unavailable in this snapshot. The internal low-temperature DS18B20 on GP26 and
external MAX31865 RTD have separate identities. BMP280 pressure on shared
GP2/GP3 is acquired and included as record 5 when fresh and valid. The connected MAX31865 is identified as the external RTD; its existing
-0.89 C correction is applied by its sensor driver before serialization.
All temperature records use signed hundredths C (-55..125 C for the internal DS18B20, -200..327.67 C for the
external RTD; DHT -40..80 C). This deliberately covers the requested cold range; an RTD above
327.67 C is omitted by this balloon producer rather than overflowing.

`sendCamera(CameraView, now)` copies a JPEG plus width/height. A 32 KiB image
fits in one logical message. This API is available, but automatic camera LoRa
transmission is **not enabled**. The existing camera USB output is unchanged.
Ground applications can implement `onCamera` to store/display complete images;
the current ground USB bridge exports sensor messages, not full image messages.
Large messages are counted as dropped by that diagnostic USB sink rather than
silently truncated into misleading hex.

`send(MessageView, now)` also accepts opaque legacy/custom bytes. The sender
assigns IDs; identities in MessageView are receive metadata. `Ok` means the
message was copied into the single outbound slot. `Busy` means it is occupied;
`NotReady` means no caller storage was supplied. `poll` attempts packets and
retries local backpressure after 250 ms. `lastSendResult()` exposes the most
recent packet submission result. No queued-slot acceptance proves remote receipt.

Callbacks borrow bytes only until return. `onMessage` receives all complete
validated messages, then typed envelopes invoke `onSensors` or `onCamera`.
Malformed typed envelopes do not reach either application callback. Reentrant
sends/fragment delivery from a callback return Busy. Legacy messages remain raw.

## Dense wire records, version A1

All multibyte integers are big-endian; signed values use two's complement.
A message starts with `0xA1` (format/version) and `uptimeMs:u32`, followed by
records. Each record has `type:u8`, `length:u8`, then its bytes. A length of 255
introduces `extendedLength:u16` before the value. Ordinary sensor records cost
two header bytes each. IDs are unique within a message; unknown IDs, duplicates,
wrong lengths, truncation and invalid ranges are rejected. A future incompatible
schema must use a new version; records cannot silently change meaning.

| ID | Identity | Value representation |
| --- | --- | --- |
| 1 | Internal DHT temperature | i16, hundredths C |
| 2 | Internal DHT humidity | u16, hundredths %RH |
| 3 | Internal low-temperature DS18B20 | i16, hundredths C |
| 4 | External low-temperature RTD | i16, hundredths C |
| 5 | Barometric pressure | u32, Pa |
| 6 | MAX-M10S GPS | flags:u8, latitude:i32 millionths degree, longitude:i32 millionths degree, altitude:i32 decimeters, satellites:u16 |
| 7 | Camera | width:u16, height:u16, JPEG bytes |

GPS flag bits are stream=1, position=2, altitude=4, satellites=8; zero numeric
values are valid when their flag is present. A camera record is the only record
in its message. Sensor snapshots can contain any combination of IDs 1..6.
GPS + DHT temperature/humidity + both probes + BMP280 pressure occupies
**44 bytes** with all six records valid (38 without pressure, 34 without
pressure and the internal probe). No sensor names, units or JSON travel
on air. Python's `message_codec.py` implements the same schema for the desktop
bridge, verified against a shared C++ golden byte vector. The bridge preserves
legacy HABT-v1 decoding and exposes new readings under `sensors`, including
`internal_low_temperature_c` and `external_rtd_c`, without replacing the DHT `environment.c` field. This does not
add an RTD panel to the existing dashboard UI.

## Fragmentation, memory and failure behavior

The protocol's existing 13-byte data header already carries session/message ID,
fragment count and fragment index; no additional fragment header is added.
Packet payload capacity remains 240 bytes. Message size is 1..32784 bytes,
at most 137 fragments; nonfinal fragments are full. A 32768-byte JPEG plus its
13-byte typed envelope uses 137 fragments. The packet layout/version is unchanged,
but old firmware's 512-byte validator rejects large transfers, so both radio
coprocessors also need the updated validator before using large messages.

RP2040/native Storage contains one 32784-byte TX and one 32784-byte RX buffer;
there are no heap allocations. Legacy AVR MessageManager storage, if explicitly
supplied, is limited to 512 bytes; the split radio MCU does not instantiate a
MessageManager and buffers only individual packets.

There is one outbound slot and one in-progress multi-fragment receive slot.
Receive fragments may arrive out of order; identical repeats are ignored and
conflicting duplicate bytes invalidate the assembly. A different multi-fragment
message returns Busy while that slot is occupied; unrelated single-fragment
messages can still be delivered. Missing fragments prevent delivery. The slot
expires after 120 seconds without a new fragment (wrap-safe millis arithmetic).
The last eight completed identities are suppressed for 120 seconds; this is a
bounded duplicate filter, not persistent exactly-once delivery. Sender jobs
expire after one hour. Transmission is gated by PacketService/LinkManager;
the balloon retains its profile-dependent pause after estimated packet end.

Per-packet CRC protects each fragment. There is **no retransmission/replay
cache, remote delivery acknowledgement or automatic missing-fragment recovery**
in this change. LinkManager's existing retransmission request interface remains
unconnected to message storage. Those reliability policies can be built on top
of this interface without moving packet encoding back into the application.

## Validation and deployment

Native ASan/UBSan tests cover sensor identities, signed cold temperatures,
malformed/duplicate records, reverse fragment order, missing/conflicting
fragments, timeout across millis wrap, and complete 32 KiB camera transfer
through real packet codecs and simulated radio. Split tests cover transfer
through simulated RF and the byte-paced host/radio UART. Python tests check the
same golden vector and expose separate DHT/RTD values through the ground API.

The MessageManager source was first built/tested without deployment. The
subsequent DS18B20 integration was flashed to the balloon RP2040, including
this MessageManager runtime. Ground source is updated/built but the ground host
and radio coprocessors were not reflashed in that deployment. Deploy matching
ground firmware and updated radio coprocessors for large messages. There is no
two-endpoint hardware verification for this refactor. See
[DS18B20 deployment report](../test-results/2026-10-23-ds18b20-gp26-retest/REPORT.md).

The legacy single-AVR `transmitter` build exceeds its 28672-byte flash limit.
A reconstructed build with the original MessageManager and 512-byte validator
also exceeds that limit (34514 bytes); the new manager build is 37356 bytes.
This refactor targets the working split host/radio architecture.
