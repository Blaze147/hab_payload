# Protocol version 1

ProtocolManager now encodes, CRC-protects and submits data, control and beacon
packets to RadioManager. It also decodes received wire packets, validates CRC
before trusting header identities, records receive observations and dispatches
typed callbacks. Link policy belongs to LinkManager; see [LinkManager.md](LinkManager.md)
for the implemented acquisition, handshake, profile switching and recovery rules.

Every packet starts with one version byte (1), followed by one packet-type byte:
Data=0, Beacon=1, Control=2. All multi-byte integers are unsigned big-endian unless
otherwise specified. Structures are logical containers, never packed wire images:
serialize using explicit offsets, never sizeof or native-memory casts. Unused
array entries and native padding are never transmitted. Maximum packet size is
255 bytes. The current definitions are in the `protocol` namespace.

## Data packet

| Offset | Field | Wire size |
| --- | --- | --- |
| 0 | Version (1) | 1 byte |
| 1 | Type (Data=0) | 1 byte |
| 2 | Transmitter session ID | 2 bytes |
| 4 | Message ID | 2 bytes |
| 6 | Fragment count (total fragments in message) | 2 bytes |
| 8 | Fragment index (zero-based) | 2 bytes |
| 10 | Retry count (0 for first attempt) | 1 byte |
| 11 | Flags (data flag bits unassigned) | 1 byte |
| 12 | Payload length | 1 byte |
| 13 | Payload | N bytes |
| 13+N | CRC-16 | 2 bytes |

The 13-byte header and two-byte footer leave 240 payload bytes. The sender's
session ID is its boot session: A-to-B packets carry A's session, B-to-A packets
carry B's. Message IDs group fragments within that session. All 16-bit ID values
are representable. MessageManager generates boot-session/message identities, advances the session
on message-ID wrap and provides bounded stale/duplicate filtering. Retry overflow policy and data flag semantics are
also pending; all data flag values are accepted structurally.

`DataPacket` stores header, payload and CRC. `Packet` aliases DataPacket and
`DataHeader` aliases Header. The message-size validator accepts 1..32784 bytes,
at most 137 fragments. Nonfinal fragments contain exactly 240 bytes; the last
fragment is bounded by the total-message limit. MessageManager owns the complete
message buffers and typed codec; the radio processor still buffers only packets.
See [MessageManager.md](MessageManager.md). This extends the previous 512-byte
validator without changing packet header fields or on-air packet size.

## Control packet

| Offset | Field | Wire size |
| --- | --- | --- |
| 0 | Version (1) | 1 byte |
| 1 | Type (Control=2) | 1 byte |
| 2 | Conversation ID | 1 byte |
| 3 | Flags | 1 byte |
| 4 | Desired balloon data profile | 1 byte |
| 5 | Window timing slide | 1 signed byte, 10 ms units |
| 6 | Target retransmit message ID | 2 bytes |
| 8 | Retransmit group bitmap | 2 bytes |
| 10 | Fragment bitmaps for selected groups | 2 bytes each |
| 10+2K | CRC-16 | 2 bytes |

K equals the population count of the group bitmap, 0..16. Total packet length
is exactly 12+2K bytes: 12..44 bytes including CRC. `retransmitBitmapCount` is
local metadata, not an extra wire byte, and must equal K.

### Two-level retransmission map

Group bit g selects indices 16*g through 16*g+15. Its fragment bitmap's bit b
requests index 16*g+b. Bits are numbered from the least significant bit (zero).
Selected groups' maps appear in ascending group order, each map big-endian.
This represents indices 0..255 without sending maps for unselected groups.

For fragments 7,12,15,22,25: group map 0x0003, then fragment maps 0x9080 and
0x0240. Wire bytes for these fields are `00 03 90 80 02 40`. Structural validation
does not require selected fragment maps to be nonzero or check indices against
a particular stored message. LinkManager's retransmission source decides whether
requested fragments are available. Real retention/replay storage is still pending.

### Conversation and fields during handshake

`ControlFlag` values: None=0, SYN=1, ACK=2, SYN-ACK=3. SYN-ACK combines SYN and ACK
bits; these are this protocol's markers, not TCP's wire flags. Structural packet
validation accepts the flag byte; LinkManager checks allowed stages and roles.

Ground starts ID 1 each control window; failed attempts advance the ID within
that window. It never wraps 255 to zero mid-window. End of window resets the ID
to zero. A new attempt discards prior uncommitted choices, not message data.

Ground SYN -> balloon SYN-ACK -> ground ACK all carry useful control information.
The profile field is always an absolute desired ID, not a delta or empty-field
sentinel. Repeat P2 as value 2 to keep P2; value 0 explicitly requests P0. Balloon
echoes the profile to accept it for the next data window. Current control remains
P0. Only ground drives balloon data-profile adaptation.

For a nonempty retransmission request, balloon echoes the exact group map and
fragment maps when accepting; a zero group map/count means unavailable data.
Partial nonzero acceptance is not supported. Ground ACK echoes the response's
accepted fields. A zero request is simply no request, not proof of unavailable
data. Availability is checked through an optional source; without one, nonempty
requests are refused. Ground can inspect acceptance/refusal in LinkManager.

Timing slide is balloon-to-ground only, with no echo requirement. Zero means no
adjustment. Positive delays the ground's next control boundary, negative advances
it; multiply by 10 ms (-1280..+1270 ms). The implemented reference is estimated
packet start, derived from RX completion minus airtime, with a 50 ms quiet target.
A start at +60 ms yields -1, an advance of 10 ms. Changes are staged through the
handshake and applied once to the following ground data-phase duration; balloon
never shifts its window. Early-ID retry heuristics are configurable in LinkManager.

The final ACK can be lost: balloon commits only on receipt, ground only knows
local ACK transmission completed and labels its completion provisional. Incomplete
balloon proposals are dropped on timeout/end of window. P0 control windows provide
a common recovery rendezvous if data profiles temporarily disagree. Full behavior
and limitations, including stale packets across window-ID reuse, are in LinkManager.md.

## Beacon packet

| Offset | Field | Wire size |
| --- | --- | --- |
| 0 | Version (1) | 1 byte |
| 1 | Type (Beacon=1) | 1 byte |
| 2 | Time until next conversation window | 2 bytes, 100 ms units |
| 4 | Conversation duration | 1 byte, whole seconds |
| 5 | CRC-16 | 2 bytes |

Exactly seven bytes. Countdown 1 means 100 ms; 1200 means two minutes. The full
range is 0..6553.5 seconds, converted to milliseconds with 32-bit arithmetic.
Duration validation accepts 1..120 seconds, default 10. Countdown is not capped
at 120 seconds since it describes time *until* a window rather than its length.
A structural zero countdown is representable; LinkManager rejects it as an
acquisition announcement that cannot identify a future boundary.

Balloon acquisition starts with data on P0, followed by beacon/data/listening
slots. Beacons announce the next listening slot from estimated receive completion,
rounded upward to 100 ms. Ground starts P0 searching and uses them for coarse
alignment. Control timing slides refine it in 10 ms steps. The guard, packet
completion budget and 500 ms completion-to-next-beacon wait are implemented by
LinkManager rather than packet codecs.

## CRC and receive Fragment

All packet families use CRC-16/CCITT-FALSE: polynomial 0x1021, initial 0xFFFF,
non-reflected input/output, xor-out 0. ASCII `123456789` gives 0x29B1. It covers
exactly the header and actual payload/bitmap bytes before the final two-byte
big-endian CRC. This protocol CRC is independent of hardware LoRa CRC.

`receiveRaw()` checks CRC before trusting packet IDs or indices. Corrupt/malformed
packets are measured but never passed to MessageManager. Valid data becomes an
owned `Fragment` containing decoded header, full wire bytes, actual length, a
payload view, receive timestamp/RSSI/SNR/raw modulation and classified profile.
The reusable workspace is borrowed for the callback duration. Copy it if retained.
`acceptDecoded()` remains a trusted test/integration seam: structurally valid
inputs are marked CRC Unchecked and excluded from the corruption-rate denominator.

RadioManager stamps raw receive modulation settings, not named profiles. Profile
classification uses LinkManager's ancillary definitions; absent/custom settings
have an explicit unknown bucket. Hardware CRC rejection surfaces CrcError with
metadata and no trusted bytes; protocol CRC errors are detected on raw receive.
Physical IRQ/timestamp behavior still needs board validation.

## Profiles

| Profile / ID | SF | Bandwidth | Coding rate | Intended use |
| --- | --- | --- | --- | --- |
| P0 / 0 | 12 | 125 kHz | 4/8 | Discovery, control, recovery |
| P1 / 1 | 10 | 125 kHz | 4/6 | Long-range data |
| P2 / 2 | 9 | 125 kHz | 4/5 | Normal data |
| P3 / 3 | 8 | 250 kHz | 4/5 | Fast data |
| P4 / 4 | 7 | 500 kHz | 4/5 | Maximum-speed data |

Definitions live in LinkManager/src/LinkProfiles.h. Profiles alter SF, bandwidth
and coding rate only, preserving frequency/power/preamble/sync word/CRC settings.
Defaults and recovery use P0. LinkManager now implements initial probing,
asymmetric hysteresis, two-silent-control data fallback and four-cycle acquisition
recovery. These initial policy thresholds need RF tuning; intended uses are not
measured range/throughput guarantees. Statistics compute metrics; LinkManager
owns decisions and timing.
