# Link runtime

LinkManager now runs the balloon/ground state machine. ProtocolManager encodes
and transmits data, beacon and control packets with CRC-16; RadioManager stays
a wrapper over raw radio settings and hardware. No heap allocation is used by
the production managers. The native tests exercise real encoded packets through
the simulator, not just calls into placeholder callbacks.

## Startup and phases

Call `ProtocolManager::begin()`, register MessageManager/LinkManager with their
`begin()` methods, initialize RadioManager, then call `LinkManager::start(role,
config, now)`. The default main does this with the transmitter as Balloon and
the receiver as Ground. Poll ProtocolManager, MessageManager and LinkManager
regularly using the same unsigned millisecond clock.

- Balloon starts with a 30-second P0 data phase. Application data is separate;
  LinkManager creates no synthetic data. ProtocolManager's data gate permits
  balloon data only during this phase and rejects a packet whose calculated
  airtime would reach or exceed its scheduled end.
- Acquisition alternates a beacon slot and a control-listening slot, with a data
  phase between them: data -> beacon -> data -> control -> repeat.
- Ground starts Searching on P0. A valid beacon sets its upcoming control-window
  boundary. Successive acquisition beacons can refine that estimate. An
  established ground station immediately drops its old agreement on a valid P0
  beacon and adopts the announced boundary, including during a control attempt.
  Data-profile/probe state resets to P0 so a rebooted balloon can reacquire.
- Control and beacon slots use P0 and last 10 seconds by default. Both honor a
  50 ms initial quiet guard. In beacon mode, balloon waits 500 ms after each
  completed beacon before the next and avoids starting one that cannot finish
  before the boundary. It does not listen for a normal handshake in beacon slots.
- A beacon announces the next *listening/control* window, skipping the intervening
  data phase. Its 100 ms countdown is measured from estimated RX completion,
  using calculated airtime, rounded upward. Hardware/poll timing introduces error;
  control timing slides refine it later.
- Once established, every conversation slot is a normal P0 control slot.

Durations are configurable (`dataWindowMs`, `controlWindowSeconds`); the data
phase is capped at 120 seconds and control duration at 120 whole seconds. The
frame budget still has to fit real airtime. A very short configured control
window can fit no handshake and produces an Exhausted attempt, not a false link.
`start()` requires initialized hardware. Hardware faults enter Fault and disable
data; explicit radio recovery and restarting the link are required. Stopped
links do not generate traffic. No firmware is uploaded by the build/test tools.

## Pending conversation and commit

Ground initiates ID 1 after the guard. Packet flags are SYN=1, SYN-ACK=3, ACK=2.
All three packets carry useful control fields. Proposed settings remain separate
from the current committed data profile throughout the exchange.

1. Ground sends SYN with the desired absolute balloon data-profile ID, target
   retransmit message ID, group bitmap and fragment bitmaps. Its timing slide is
   always zero. Repeating the current profile means retain it; zero means P0.
2. Balloon stores the request and sends SYN-ACK echoing the accepted profile. It
   echoes the retransmit request if the registered source says it is available;
   otherwise group map/count are zero. The response can carry a timing correction.
3. Ground validates the echo, stores the response and sends ACK. ACK echoes the
   accepted profile and accepted retransmit fields, with timing slide zero.
4. Balloon commits only when it receives the matching ACK in the same window.
   Ground commits locally only after its ACK transmission completes, explicitly
   exposing Conversation::Provisional rather than confirmed remote receipt.

Radio modulation stays P0 for the entire control window. The committed data
profile is applied at the next data boundary: balloon transmits and ground
receives using it. `dataProfile()` during control describes that committed next
profile. Actual files/messages are never deleted on handshake failure.

SYN/SYN-ACK timeouts begin after local TX completion. Default reply timeout is
4 seconds, with a 50 ms turnaround delay. Ground discards a failed attempt and
uses the next ID if enough time remains for a full worst-case three-message
exchange. Balloon discards a timed-out proposal and waits for a newer SYN. New
IDs supersede pending proposals; older IDs and conflicting same-ID requests are
rejected. IDs do not wrap within a window; 255 exhausts the attempt space. The
end of a window resets the ID to zero and drops uncommitted proposals.

Duplicate pending SYNs reuse the cached response without reserving data twice.
Duplicate completed ACKs do not repeat committed effects. A duplicate SYN-ACK
causes ground to repeat the ACK without applying the profile/timing a second time.
Control flags, matching IDs/profiles/maps, P0 receive settings, receive length
and window boundaries are checked in addition to ProtocolManager's CRC checks.

### Lost final ACK

Three packets cannot prove mutual commitment over an unreliable link. If the
ACK is lost, balloon times out and discards its pending proposal; ground cannot
observe that loss and can remain provisionally on the requested data profile.
A data phase can therefore be missed. Their P0 control windows remain a common
rendezvous for recovery and a new conversation. No fourth confirmation is
invented, and the API does not label the ground's final ACK as proof of delivery.
`established()` means local handshake completion; inspect `conversation()` for
Complete (balloon) versus Provisional (ground).

The one-byte conversation ID resets each window and the wire format has no
window epoch. Arbitrarily delayed/replayed packets from older windows are not
fully distinguishable when IDs and content coincide. There is no authentication.

## Timing correction

Balloon is the timing authority. Positive timing slide delays the ground's next
control boundary; negative advances it. This resolves the earlier ambiguous
“forward” terminology. The first SYN's estimated RF start is its RX timestamp
minus calculated frame airtime. The target is start+50 ms, so an estimated start
at +60 ms produces -1 (advance 10 ms), not a 60 ms correction.

The signed request is clamped to -128..127 units of 10 ms and is staged until
ground's local handshake completion. It modifies the following data-phase
length once, aligning the next control start, rather than shifting the current
control slot midway through a handshake. A failed/uncommitted attempt drops the
slide; duplicates cannot apply it twice. The balloon rejects nonzero timing
slides from ground and never shifts its own schedule.

If the first observed attempt has ID >1 and estimated start <=100 ms, balloon
uses a configurable +100 ms delay request as the early-retry heuristic. This is
an inference, not proof: packet loss can produce the same ID. Hardware timestamp
accuracy and these provisional tuning values need RF validation.

## Profiles and fallback

Only ground executes automatic profile selection for balloon data. Role-aware
code is shared by both devices. Data and control profiles are distinct; control
always uses P0 regardless of the data rate.

The ground split application enables automatic selection. The policy requires four
valid data observations before a trial decision, accumulating across windows.
It scores the last four observed data/error packets in the current profile instance using unique
payload bytes divided by RF airtime. Valid beacons/control packets do not occupy
that ring. Duplicate/retry attempts and corrupt/malformed observations contribute
cost without duplicate useful bytes. Dense statistics storage is required;
without it automatic selection holds the current profile.

Startup exploration steps P0 -> P1 -> ... while candidates improve estimated
goodput by more than 5%. At most 25% observed corruption is allowed. A failed
probe returns to the previous best profile. After exploration stops, upward
exploration restarts after 16 minutes, measured in elapsed milliseconds; a
successful periodic probe continues exploring successive profiles. Each actual data-profile
change starts a new instance and a fresh trial sample period; the previous instance is retained. Three consecutive silent data windows request a direct fallback to P0,
even if P0 control exchanges succeed. The first two hold the current profile. Recent airtime goodput more than 25% below the current whole-instance
airtime goodput for three consecutive eligible data windows also steps down. All thresholds are
configurable starting policies, requiring live RF validation.

This is an **observed-attempt airtime estimate**, not complete delivery goodput:
entirely unheard transmissions cannot be charged without sender attempt reports.
Partial packet loss can therefore overestimate performance. No sender attempt
report protocol is implemented. The estimate intentionally excludes idle time;
existing wall-clock goodput APIs retain their original meaning.

`requestDataProfile()` provides a ground-only manual target and disables automatic
probing for that run. It and `requestRetransmission()` reject mutation during an
active conversation. `setProfile()` remains a low-level local diagnostic setting;
applications should use negotiated data profiles during normal operation.

A control slot with no valid matching peer exchange increments the silent-window
counter. A valid SYN on balloon, or matching SYN-ACK on ground, counts as contact;
this is distinct from completing the handshake. Two consecutive silent control
slots return the data profile directly to P0. Four trigger beacon reacquisition;
ground returns to Searching. Beacon slots are not counted as failed control
listening slots. Successful contact resets the counter. Partial failed handshakes
that contain valid contact can therefore postpone silence-based reacquisition.

## Retransmission integration and remaining work

A registered RetransmissionHandler checks availability, queues committed requests
through `retransmit()`, and releases aborted proposal reservations through
`discard()`. Sources must keep accepted data available until commit or discard.
There is no message cache in LinkManager. Without a source, nonzero requests are
refused explicitly. `retransmissionAccepted()` / `retransmissionRefused()` expose
the last committed response until the next commit or restart, independently of
later edits to the configured request. Replacing the source is rejected while
a proposal is pending. A zero request
is neither a refusal nor a nonempty acceptance. Requests remain configured until
the application updates/clears them; LinkManager does not invent message recovery
lifecycle or clear payload data.

MessageManager's outbound fragmentation, multi-fragment assembly, replay cache,
boot session/message allocation and application file/data producer remain future
work. Packet-level send/receive and link acquisition/negotiation now execute, but
that does not complete the application message-transfer service. The 512-byte
message cap is unchanged; see Reminders.md. Physical RF and peak-stack validation
remain pending even when native tests and AVR builds pass.


## Verification

Twenty-one native test groups and the ASan/UBSan harness pass, including encoded
packet round trips, acquisition, successful transactions, lost SYN-ACK/final ACK,
restarts, duplicate ACK/SYN/SYN-ACK handling, refusal, reservation discard,
wrong-profile/stale-frame rejection, profile sampling across windows, two/four
window fallback, deadline guards and unsigned clock wrap. All eight AVR variants (including two integrated test builds)
build. Default static SRAM is 1,979 / 2,560 bytes; flash is 23,572 / 28,672.
An initial physical P0 bench test now passes; see
../test-results/2026-10-21-p0-hardware/REPORT.md. Extended RF and peak-stack
validation remain pending.


The integrated P0 test additionally sends 30 encoded counter packets through
this runtime, validates single-fragment delivery at ground, and exercises data
before and after acquisition. It caught and corrected unnecessary reconfiguration
when P0 was already active: matching profile settings now leave RX uninterrupted.
See LINK_TEST.md for actual results and board instructions.

## Probe negotiation persistence

Probing is a mode spanning negotiation and candidate evaluation. A selected
candidate remains the target across failed conversations and control windows;
the unchanged operating profile is not evaluated as the candidate. Ground
retries after timeouts while another SYN fits within the control window. Late
attempts may lack time to complete all three frames; each frame must still fit
before the boundary. No retry may transmit across that boundary.

By default, three completed control windows without local completion of the
candidate handshake end that probe negotiation (`maximumProbeNegotiationWindows`).
A successful negotiation in the third window still proceeds to evaluation.
Exhaustion keeps the operating profile (subject to the existing silence fallback)
and schedules the next exploration 16 minutes later. Silence fallback does not
erase a pending candidate before the three-window limit.

Ground commits provisionally after transmitting the final ACK: this is local
completion, not proof of balloon receipt. The existing silent-candidate fallback
still applies if that ACK is lost; this change does not add remote confirmation.
USB status exposes `probing`, `probeNegotiating`, and `failedProbeWindows`.

## Evidence-driven upgrades and hysteresis

Periodic exploration defaults to **16 minutes**. Startup still explores after
synchronization and enough baseline samples; neither startup nor periodic
exploration requires a special signal improvement.

Ground records the maximum reliable rolling airtime-goodput score observed for
each profile across its two retained instance records. This is an empirical reference, not a theoretical
capacity or a guarantee that every profile can be measured at startup.
Unreachable profiles remain unmeasured. The peak is available through
`peakAirtimeGoodput(profile)`. Wall goodput remains available for reporting.

A separate evidence trigger can attempt the next profile before the periodic
interval: three consecutive eligible data windows must have rolling airtime
goodput at least 90% of that profile's recorded peak. Existing minimum sample,
corruption, and tracking checks also apply. Evidence-driven upgrades evaluate
one step rather than automatically sweeping all higher profiles. They share
the existing negotiation, three-window handshake budget, and trial evaluation.

Before each upgrade, ground remembers the source profile's goodput and mean
SNR from the same rolling data/error samples. If negotiation or the faster
trial fails, early retries of that transition require at least two minutes
and either 10% better source-profile goodput or 3 dB better mean SNR than at
the failed attempt. Being near the same historical peak is insufficient on
its own. The 16-minute periodic exploration bypasses this evidence restriction.
A successful faster trial still requires more than 5% goodput improvement.

Thresholds are configurable starting values: `nearPeakFraction`,
`strongWindowsForUpgrade`, `evidenceRetryCooldownMs`,
`evidenceGoodputImprovement`, and `evidenceSnrImprovementDb`. History is RAM-only
and resets on link start. UART status exposes `peakAirtimeGoodputBps` and
`probeReason`: 0 none, 1 startup, 2 periodic, 3 evidence.

## Consecutive silent data windows

Automatic operation counts completed data windows with no valid data packets.
Any valid data reception resets the count, and an agreed profile change starts
a fresh count. After three consecutive silent windows
(`silentDataWindowsBeforeFallback`), ground cancels the current speed trial and
requests P0 directly. This recovery target persists across failed handshakes
until the P0 agreement completes. P0 control/beacon traffic cannot reset the
data-silence count. USB status exposes `silentDataWindows`.

This rule is separate from the existing two missed control windows triggering
local P0 fallback, four missed control windows triggering acquisition, and three
failed negotiation windows ending a probe. Manual profile selection still
disables automatic policy, including this data-silence rule.

## Per-instance statistics

StatisticsManager stores two `ProfileRecord` objects per profile. Each has a
four-packet recent view and a cumulative whole-instance view for wall/airtime
goodput and signal measurements. LinkManager closes sampling during control
slots but keeps the same data-profile record. A switch rotates records only
when the new data phase starts. Refer to StatisticsManager.md for lifecycle,
retention, denominator definitions and diagnostic compatibility APIs.

## Whole-instance degradation reference

Degradation now compares the latest four-packet airtime goodput with the
cumulative airtime goodput of the **same current ProfileRecord**. The preceding
short-term score is no longer the degradation reference. `bestGoodput_` remains
only the saved operating score for comparing a faster candidate trial.

The reference must have at least twice `minimumProbePackets` valid data
observations (eight by default), contain observed airtime outside the recent
four-packet ring, and have no tracking/airtime incompleteness. This prevents
classifying a fresh instance from its previous visit's statistics or comparing
a recent sample only against itself. The minimum probe count is bounded by
the four-packet ring capacity.

During normal automatic operation, recent airtime goodput strictly more than
`maximumGoodputDrop` (25%) below that reference increments a consecutive-window
counter. After `degradedWindowsBeforeFallback` (three) eligible windows, ground
requests one profile slower. The target persists through failed handshakes until
negotiated, or until a stronger recovery returns directly to P0. A pending
degradation downgrade takes priority over upward probes.

Recovery, silence, incomplete statistics, insufficient samples, profile-instance
changes and candidate negotiation/evaluation break the degradation streak.
Existing observed-corruption/failed-trial handling still applies independently.
Three silent data windows still request P0 directly. Manual mode disables this
automatic policy. USB status exposes `degradedDataWindows`.

The reference includes new packets in its cumulative byte/airtime totals; it is
not a fixed peak or an unbounded packet history. It can gradually adapt during
a long decline, but moves much more slowly than the recent four-packet view
once a useful baseline has accumulated. Entirely unheard transmissions remain
invisible to both airtime estimates.
