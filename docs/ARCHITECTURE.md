# Communication module responsibilities

The default deployment is now split across two processors; see
[SPLIT_DEVICE.md](SPLIT_DEVICE.md). The responsibilities below apply to both the
local implementation and the split PacketService transport.

The modules have connected interfaces and a working packet-level link runtime.
Typed data serialization and bounded multi-fragment message transfer are implemented; see [MessageManager.md](MessageManager.md).

| Module | Ownership and current behavior |
| --- | --- |
| RadioManager | Thin hardware wrapper: raw configuration, bytes, TX/RX states and receive metadata. No named-profile policy. |
| ProtocolManager | Encodes/decodes data, control and beacon packets; computes/checks CRC; creates owned Fragments and reports receive observations. |
| MessageManager | Owns typed codecs, IDs, outbound fragmentation and bounded reassembly; replay/retransmission storage remains pending. |
| StatisticsManager | Holds per-profile aggregates, provisional gaps and optional dense recent summaries; it does not choose profiles. |
| LinkManager | Runs role-aware windows, beacon acquisition, pending SYN/SYN-ACK/ACK conversations, timing corrections, profile selection and recovery. |

## Connections

| Diagram arrow | Interface |
| --- | --- |
| Application -> MessageManager | `sendSensors()`, `sendCamera()` or `send(MessageView, nowMs)` |
| MessageManager -> Application | `setReceiver()` / `messaging::Receiver::onMessage()` |
| MessageManager -> ProtocolManager | `sendData()` packet endpoint; manager fragments complete messages |
| ProtocolManager -> MessageManager | `DataReceiver::onFragment()` |
| LinkManager -> ProtocolManager | `sendControl()`, `sendBeacon()`, `configureRadio()`, data transmission gate |
| ProtocolManager -> LinkManager | `LinkReceiver::onControl()` / `onBeacon()` |
| ProtocolManager <-> RadioManager | injected reference on the radio board, TX/RX, polling and raw configuration |
| ProtocolManager -> StatisticsManager (green) | compact receive observations and radio errors |
| MessageManager -> StatisticsManager (green) | delivered messages and invalid message inputs |
| StatisticsManager -> LinkManager (green) | overall, historical per-profile and optional recent snapshots |

LinkManager and MessageManager depend on PacketService; the local implementation
is ProtocolManager and the split host implementation is RemoteProtocol.
ProtocolManager reports observations through packet_statistics::Sink;
StatisticsManager implements that sink locally and RadioServer forwards it remotely.

Constructors require dependencies. Begin ProtocolManager, then consumers; register
application callbacks after MessageManager::begin(), which clears its receiver.
Dependencies and receivers must outlive their users. Null receiver disconnects.
Callbacks borrow arguments only until return. Dispatch is synchronous in the main
loop and is not reentrant; do not call receive/poll recursively from a callback.
Production managers use no heap or allocation-based queues.

## Default application and lifecycle

Default main initializes radio, starts LinkManager as Balloon (`transmitter`) or
Ground (`receiver`), and polls all managers. Balloon enters P0 data immediately,
then acquisition: data -> beacon -> data -> control-listen. Ground searches P0,
learns its control boundary from beacons and initiates SYN after the quiet guard.
Successful conversations retain normal control slots; control always stays P0.
Default firmware can now transmit beacons/control traffic when flashed. It does
not synthesize application messages. Hardware-test variants remain independent
direct-radio diagnostics. No build command uploads firmware.

See [LinkManager.md](LinkManager.md) for implemented transitions, configurable
policy defaults, provisional ground-side commit and loss behavior. The final ACK
cannot prove delivery at ground; that ambiguity is explicitly exposed. P0 control
provides a shared recovery point after data-profile disagreement.

## Packet and message boundaries

`receiveRaw()` verifies CRC before trusting identities, validates fields and
routes packets. Data enters a reusable owned Fragment containing wire bytes,
decoded header, payload view, local profile and receive metadata. Copy it to retain
it after callback. RadioManager supplies raw receive SF/bandwidth/coding rate;
profile classification uses LinkManager's ancillary LinkProfiles definitions.
`acceptDecoded()` is a trusted compatibility/test seam: it validates structure,
but its CRC is unchecked and excluded from corruption-rate denominators.

Packet send methods now encode bytes, insert CRC and request nonblocking TX.
`Ok` means the local radio accepted the request, not remote delivery. `Busy`,
`NotReady` and `HardwareError` expose backpressure/failure. Data uses the reusable
workspace outside fragment callbacks; sends during those callbacks return Busy.
The data gate also checks estimated airtime against the window deadline.
Adapters must consume/copy transmit bytes before their transmit() call returns.

MessageManager buffers complete messages, fragments outbound data, reassembles
inbound data and decodes typed records before application callbacks. The RP2040
runtime supplies fixed TX/RX storage and a random boot session. Applications
supply typed sensor values, not wire offsets. There is one pending send and one
multi-fragment assembly; timeout/duplicate policies and wire IDs are specified
in [MessageManager.md](MessageManager.md). Retransmission/replay storage remains
pending; LinkManager still refuses unavailable retransmission requests.

## Statistics and profile decisions

StatisticsManager defaults to minimal fixed aggregates and two bounded 256-index
message trackers. Dense mode adds caller-supplied four-observation-per-profile
storage and optional global history. Mean/minimum RSSI/SNR and historical rates
require no growing packet log. See [StatisticsManager.md](StatisticsManager.md).

Only ground probes balloon data profiles and matches its receive modulation to
the requested target. It uses fresh data-window counter deltas and active data
time, requires four observations for initial trials, and favors useful goodput.
Initial climbing is quicker than later upward probes; poor reception triggers
faster downgrade. Starting thresholds are configurable and need RF tuning.
Only ground applies balloon-directed timing slides. Two silent control windows
reset data to P0; four trigger reacquisition. Named definitions remain in
LinkManager/src/LinkProfiles.h; RadioManager accepts raw settings only.
