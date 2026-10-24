# Receive statistics and profile records

StatisticsManager collects receive observations; LinkManager defines data-profile
instances and data-window boundaries. Statistics do not select radio profiles.
The split RP2040 host enables caller-owned `DenseStorage`; the radio coprocessor
retains its small diagnostic counters without allocating record storage.

## Two records per profile

`ProfileRecord` represents one uninterrupted visit to a data profile. Dense
storage retains **at most two records for each of P0 through P4**:

- Age 0: the active/current record, or the latest completed record when inactive.
- Age 1: the preceding completed record.

P3 -> P4 -> P3 freezes the first P3 record and creates a fresh current P3 record.
A third visit discards the oldest P3 record. Other profiles are unaffected.
Temporary P0 control/beacon slots do not create P0 data-profile records or split
the current instance. A failed profile negotiation does not start an instance
of the requested profile. The new instance begins when its data window begins,
not when the handshake proposes or provisionally commits the change.

The record has a unique runtime `instanceId`, profile ID, start/end timestamps,
active flag, cumulative totals and four retained packet summaries. Completed
records are frozen; their rates do not decay while operating on another profile.
Stopping/restarting the link or returning to acquisition ends the active record.
History is RAM-only: `statistics.begin()` clears it. Link restarts retain up to
two visits per profile unless statistics are explicitly reset.

```cpp
if (const auto* record = statistics.profileRecord(3)) {
    auto recent = record->recent(nowMs); // Last FOUR PACKETS in this visit.
    auto whole = record->whole(nowMs);   // All data/error observations in this visit.
    float recentAir = recent.airtimeGoodputBytesPerSecond();
    float wholeAir = whole.airtimeGoodputBytesPerSecond();
    float recentWall = recent.wallGoodputBytesPerSecond();
    float wholeWall = whole.wallGoodputBytesPerSecond();
    float recentSnr = recent.packets.meanSnr;
    float wholeSnr = whole.packets.meanSnr;
}
const auto* previous = statistics.profileRecord(3, 1);
// nullptr when absent; ages >= 2 are always unavailable.
```

Pointers refer to bounded owned storage and can be overwritten when the same
profile is revisited or statistics/logging storage is reset.

## Short-term and whole-instance metrics

Both views include observed packet counts, wire bytes, unique useful payload
bytes, unique/duplicate fragments, retries, corruption/malformed/CRC counts,
tracking limitations, last/mean/minimum RSSI and SNR, and observed airtime.
`total.dataWindows` counts completed data windows, including silent ones.

The short-term view is the latest **four data/error packet observations**, not
four data windows. Every eligible arrival updates it immediately. Older packet
detail is discarded while its contribution remains in the whole-instance
counters and packet-weighted running signal averages. No unbounded per-window
history or payload copies are stored. Whole-instance averages include the
recent packets, rather than being an average excluding them.

Only data packets and corrupt/malformed receptions on the active data profile,
received within its data window, enter these records. Valid control/beacon
packets do not affect the metrics or displace recent packets. The receiver
cannot identify a corrupted packet's type, so observed errors during the data
window are charged conservatively to that instance.

- **Wall goodput:** sum of unique payload bytes / elapsed wall-clock seconds.
  For the whole instance, time runs from instance start to now (or frozen end).
  For recent packets, it runs from estimated RF start of the oldest retained
  packet to now (or frozen instance end). Idle gaps and intervening control
  windows count as elapsed time. Silent windows reduce wall goodput.
- **Airtime goodput:** sum of unique payload bytes / sum of observed attempt
  airtime. Idle time is excluded. Duplicates, retries and corrupt/malformed
  attempts consume airtime without adding duplicate/invalid useful bytes.

Rates are calculated from summed bytes and summed denominators, not by taking
an unweighted average of per-packet rates. Zero denominators return zero.
Missing airtime or deduplication capacity marks the estimate incomplete via
`trackingUnavailable`; LinkManager does not make upgrade decisions from it.
Entirely unheard transmissions cannot be charged without sender attempt reports,
so airtime goodput remains an observed-attempt estimate.

ProtocolManager calculates airtime from captured receive modulation and the
configured CRC/preamble. UART observations carry it to the host. Updating that
UART format requires matching host/radio firmware within an updated pair; the
over-the-air data and control formats are unchanged.

## Lifecycle and policy integration

`beginDataWindow(profile, start, end)` creates a new instance only when no
instance is active or the data profile changes. Reopening identical bounds is
idempotent. `endDataWindow(end)` closes the sampling interval without ending the
instance. `endProfileInstance(end)` freezes it. LinkManager calls these at
actual data-window boundaries, stop/fault and reacquisition.

Automatic profile selection consumes `ProfileRecord::recent(now)` for its
four-packet airtime score and mean SNR. `recordReliableGoodput()` stores the
maximum qualifying short-term score in the active record. The historical peak
used by early-upgrade logic is the maximum across the **two retained records**;
it no longer includes discarded older visits. Probe failure/cooldown metadata
remains separately in LinkManager. Degradation detection compares recent
airtime goodput with this current record's whole-instance airtime goodput after
enough baseline observations, with a three-window confirmation threshold; see
LinkManager.md.

Host USB status reports `profileInstance`, recent `wallGoodputBps` and
`airtimeGoodputBps`, `recentPackets`, `instanceWallGoodputBps`,
`instanceAirtimeGoodputBps`, `recentMeanRssi`, `recentMeanSnr`,
`instanceMeanRssi`, and `instanceMeanSnr`.

## Diagnostic compatibility and bounded tracking

`profile(id)` returns the latest record's packet summary when a managed record
exists. Otherwise it returns low-level packet diagnostic counters. Its older
`ProfileSnapshot::goodputBytesPerSecond(now)` helper still describes the receive
observation span; use `record->whole(now)` for whole-instance wall goodput.
`packetDiagnostics(id)` explicitly reads low-level counters for hardware self-tests,
including receptions outside active data windows. Diagnostic accumulators/rings
start fresh on a managed profile's new visit.
Low-level callers without LinkManager lifecycle retain their standalone
since-reset diagnostic behavior. Overall receive/message/radio-error counters
are still device-wide.

The older four-observation `recentProfile`/`recentPacket` diagnostics and
16-attempt `recentAirtimeProfile` seam remain available for compatibility. They
are not the canonical instance metrics or the profile-selection input. They may
include control observations and should not be used to compare profile visits.
An optional caller-owned global observation history can still be configured via
`setLogging(Heavy, history, capacity, &dense)`; normal `setDenseLogging(dense)`
does not allocate that history. All storage is fixed and uses no heap allocation.
Changing logging storage clears record/sample history; `begin()` also resets
counters and message tracking while preserving the selected storage pointer.

Deduplication tracks two message identities, each up to 256 fragments, keyed by
session ID, message ID and fragment index. A profile-instance change does not
reset those identities: receiving a known duplicate on another visit does not
invent new useful delivery. A first valid retry can add useful bytes once.
Completed tracking slots may be reused; incomplete slots are not silently
reused. Exhaustion or conflicting fragment counts increments trackingUnavailable.
Very late duplicates after reuse or explicit release cannot be recognized.

Live provisional gaps remain separate from frozen instance summaries: only
indices passed by a valid arrival are called pending, and later arrivals can
clear them. `pendingFragment()` and `releaseMessage()` expose that message
tracking state. The `pendingMissing` compatibility field is not populated in
instance summaries; it is not an instance packet-loss measurement. No timeout
or unobserved tail is treated as definite loss.

Timestamps use a monotonic unsigned 32-bit millisecond clock. Subtraction
supports wrap for periods shorter than a full clock cycle; ordering comparisons
require intervals under half a cycle. Counters are finite; instance payload
counters are 32-bit and airtime totals are 64-bit.
