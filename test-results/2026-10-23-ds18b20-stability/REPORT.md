# DS18B20 steady-state stability observation

Observed the already-running full balloon firmware on GP26 for 180 seconds.
No firmware reset, flash or code change was made during this investigation.
This tests operation after the earlier wiring adjustments, rather than only
the first seconds after startup.

## Results

- 138 valid temperature reports: 21.88..22.38 C.
- Three READ_ERROR and four REQUEST_ERROR reports after monitoring began.
- One CONFIG_ERROR was the initial reported state when USB opened; that
  particular error may have occurred before observation began.
- Read/request failures at 22, 30, 38, 43, 55, 90 and 157 seconds prove the
  errors recur during continued operation, rather than only at startup.
- Sensor sequences 113..258 are contiguous: no missing status
  reports within that sequence range.
- Median interval between valid reports: 1.020 s.
- Longest interval between valid reports: 12.010 s.
- No fixed two-second failure cadence; there were long uninterrupted runs of
  good readings, including about a minute between the last two errors.

| Error observed at (seconds) | Status | Seconds until next valid report |
| --- | --- | --- |
| 0.0 | CONFIG_ERROR | 1.48 |
| 22.0 | READ_ERROR | 5.92 |
| 30.0 | READ_ERROR | 5.91 |
| 38.0 | READ_ERROR | 11.01 |
| 43.1 | REQUEST_ERROR | 5.91 |
| 55.3 | REQUEST_ERROR | 6.14 |
| 90.0 | REQUEST_ERROR | 5.99 |
| 157.4 | REQUEST_ERROR | 5.97 |

Times are host-monotonic arrival times, with the initial USB status potentially
older than the beginning of observation. They are not oscilloscope timings.

## Existing handling

InternalTemperatureMonitor.h publishes NaN/valid=false on a failed read or
request, then automatically waits five seconds before rediscovery. Repeated
failures can extend that gap; the maximum observed gap was about 12 seconds.
The five-second backoff is software behavior, not a measured five-second
physical disconnection.

TelemetryMonitor.h includes internalLowTemperature only when the latest sample
is valid and younger than six seconds. Failed/stale samples are omitted from
the typed message, not encoded as 0 C or the library's disconnected sentinel.
The desktop codec represents an absent record as unavailable. No change is
needed to implement the user's requested discard-bad-values policy. Error
visibility is retained; failures are not counted as successful readings.

The observation supports tolerating individual failed reads when occasional
measurement gaps are acceptable. It does not identify the electrical cause
or establish cold-temperature accuracy or long-term reliability.
