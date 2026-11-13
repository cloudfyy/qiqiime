# Local Performance Metrics

## Current implementation

Metrics are local, explicitly enabled, and consumed by `QiQiIMELogMonitor.exe`.
The implementation includes typed ETW events, gated RAII timing, strict decoding,
an independent bounded queue, aggregation, a Metrics view, and JSON export.

Production instrumentation currently covers **FocusTotal**, **FocusReload**, and
**FocusPosition**. Protocol support for key callbacks, candidate queries/updates,
commit, and cleanup does not mean those paths emit production samples.
There is no host-resource polling service or long-term trend collector.
Full instrumentation-overhead and cross-process acceptance remain incomplete.

To collect, stop capture, select **Metrics view**, and start **Logs + Metrics**.
Switching views while running does not change the subscription.
See the [logging guide](log.md#metrics-capture) for operating details.

## Protocol and instrumentation

The [shared protocol](../src/common/metric.h) uses schema version 1 and the existing
`QiQiIME.TraceLogging` provider:

| Event or keyword | Purpose |
| --- | --- |
| `QiQiMetric` | Typed measurement, not parsed diagnostic message text |
| `QiQiMetricInstance` | Module identity, build/architecture, provider generation, diagnostic snapshot |
| `Metrics = 0x0100` | Explicit metrics subscription |
| `Logs = 0x00FF` | Ordinary diagnostics only |

Metrics are independent of ordinary `LogLevel`, including Off. Without a metrics
subscription, synchronous scopes do not read the timing clock or allocate an
operation ID/instance GUID; the enable-state check still has a cost.

Each measurement carries:

- `schema_version`, module `instance_id`, and provider `capture_generation`;
- numeric `service_id`, `operation_id`, and `parent_operation_id`;
- finite `metric`, `phase`, `outcome`, `mode`, and `source` enums;
- integer `duration_ns` when applicable;
- explicit `error_domain` and actual `error_code`;
- PID/TID, event time, and explicit activity/related activity from ETW metadata.

The monitor's capture generation is separate from the provider's generation.
Module metadata is requested when capture starts; missing, mismatched, or stale
metadata must not be replaced with guessed architecture or version information.
Unknown versions, malformed types, invalid enums, and missing fields are rejected
from statistics with separate diagnostics.

### Timing and outcomes

[Scope](../src/common/metric.h) uses a monotonic clock and checked integer
nanosecond conversion. Callers explicitly set outcomes: destruction is not
implicit success. Unset results become `Incomplete`; exceptional exits become
`Exception`. Timing failure excludes the sample rather than fabricating zero.

Synchronous work uses `Complete`; asynchronous protocols use `Begin` and one
`End` with a stable operation ID. Distinguish success, failure, cancellation,
staleness, rejection, unhandled work, and incomplete observation.
Changing subscription mid-operation invalidates the sampled generation.

Do not change TSF threading, request order, lifetime, or input behavior to collect
metrics. Actual host writes, attribute clearing, and composition termination need
their own results; outer callback success or queue acceptance is insufficient.

## Aggregation semantics

[Aggregation](../src/tools/log_monitor/aggregate.h) is independent of the displayed
log buffer. Clearing/filtering logs or pausing auto-scroll does not alter samples.
The ETW callback validates and queues; the monitor consumes batches separately.

- Durations use 512 histogram buckets: a zero bucket, geometric finite bounds
  through 60 seconds, and an overflow bucket. Maximum duration remains exact.
- Percentiles use nearest-rank bucket upper bounds, not exact durations. P50 needs
  one sample, P95 needs 100, and P99 needs 1,000. Missing/insufficient samples and
  overflow have explicit states, not fabricated zero values.
- Success and failure timing distributions are separate. Merge counts and
  histograms before computing percentiles; do not average percentiles or rates.
- Failure rate is `failure / (success + failure)`. No denominator is not `0%`.
- Cumulative coverage is observed terminal events divided by observed intents.
  Stop-inferred incomplete operations are not observed terminal events.
- Recent statistics use the monitor's receive clock: the current second and
  previous 59 seconds. This is not an exact ETW event-time window. Recent coverage
  is not applicable; pending operations always show the current count.
- Stopping drains the queue, freezes windows, and marks pending work incomplete.
  Clearing metrics starts a new statistics interval without resetting capture
  loss counters or allowing late old terminals to count again.

Operation keys include capture generation, PID, module instance, service, and
operation ID. Duplicate events do not count twice. Remembered keys remain for
the capture so eviction cannot silently admit late duplicates.

### Storage limits and integrity

| Resource | Current limit |
| --- | --- |
| Metrics queue | 2,000 records / 4 MiB |
| Module instances | 32 |
| Services per instance | 16 |
| Dimensions per service | 16 |
| Pending asynchronous operations | 4,096 |
| Error codes per dimension | 32, then an "other errors" count |
| Aggregator allocation budget | 64 MiB |

Limits apply independently; the 64 MiB budget does not allow every dimension to
be fully populated at once. Histograms/window buckets are allocated lazily.
GUI rows, export snapshots, and serialized output are outside this aggregation
budget; it is not a process working-set limit.

Show ETW event/buffer loss, queue rejection, decode/protocol errors, allocation
failure, aggregation rejection, and provider diagnostic deltas separately.
Unattributed loss conservatively marks the capture incomplete. Clearing the
display does not make it complete; no detected loss is not proof of completeness.

Current threshold presets are 5 ms for key callback and candidate update, 2 ms
for candidate query, and 20 ms for commit. Generic focus samples have no threshold
because they do not distinguish hot checks from actual reloads.
These are engineering attention lines, not performance guarantees.

## JSON export

From Metrics view, export a versioned UTF-8 snapshot. The
[exporter](../src/tools/log_monitor/export.cpp) owns the snapshot before showing
the save dialog, so ongoing capture/clear operations cannot rewrite it.
Empty captures and filters with no matches still produce valid empty snapshots.

| Root field | Meaning |
| --- | --- |
| `format`, `format_version` | `"qiqiime.metrics"`, format version 1 |
| `tool`, `protocol_version` | Exporting tool and event schema versions |
| `capture` | Capture generation/state and observed receive-clock boundaries |
| `window` | Cumulative/recent selection, receive-clock rules, frozen state |
| `filters` | Instance, metric, outcome, and mode selection |
| `histogram_definition` | Nanosecond units, bucket bounds, overflow and percentile rules |
| `thresholds_ns` | Configured attention thresholds; unconfigured values are `null` |
| `instances` | Capture-local aliases, build/architecture, protocol and diagnostics |
| `dimensions` | Per-dimension counts, coverage, errors, and timing distributions |
| `integrity` | Loss, rejection, generation, and source diagnostic information |

Receive ticks are not UTC or exact ETW API start/stop times. The statistics start
after a clear is separate from capture start. Outcome filtering selects timing
distributions without rewriting a dimension's complete count denominators.

No sample, insufficient sample, overflow, unconfigured threshold, and missing
denominator use explicit states and `null`, not invented values. Successful and
failed histograms plus the selected timing distribution are exported separately.

Counts, service IDs, error codes, and nanoseconds are exact JSON integers,
including `uint64_t`. Readers must preserve them; JavaScript `Number` cannot
exactly represent integers above `2^53-1`.

Instances use aliases such as `instance-1`. Export does not include raw logs,
PID, module/activity GUIDs, process names, input text, or private paths.
This does not sanitize an ordinary diagnostic export made separately.

Serialized output has its own 64 MiB limit. Cancellation, serialization, or write
failure preserves the in-memory snapshot/statistics and reports the real error.
A failed target file can be incomplete; saving is not an atomic replacement.

## Validation and remaining work

Relevant suites are `QiQiMetricsTest`, `QiQiMetricsEtwTest`,
`QiQiMonitorMetricsTest`, `QiQiMonitorMetricsEtwTest`, and `QiQiFocusMetricsEtwTest`.
They exercise protocol validation, actual ETW field types, real DLL focus events,
decode/queue/aggregation, limits, and lifecycle boundaries. GUI checks are also
part of `QiQiLogMonitorTest`.

The metrics test executable supports `--json-fixtures <existing-directory>` for
independent JSON-parser checks and refuses to overwrite existing fixtures.
Run both architectures sequentially; permission-skipped integration tests are
not passing cross-process acceptance.

The [focus benchmark](../tests/ime/focus_benchmark.ps1) uses an external caller
clock and same-source inert-instrumentation baseline. It measures unchanged focus
handling, not handled keys. Private ETL capture followed by decoding is not a
normal or slow realtime consumer. Setup is in the
[development guide](dev.md#focus-performance-benchmarks).

Outstanding work includes broader production instrumentation, real-host and
cross-process scenarios, normal/slow consumer and repeated-toggle overhead
measurement, and any future resource/trend sampling. Existing focus-only results
do not establish a stable whole-pipeline performance budget. Never discard slow
samples, change denominators, or silently sample events to claim compliance.

## Privacy and maintenance

Record only timings, counters, finite state, actual errors, build/architecture,
and capture-local correlation. Do not record text, codes, candidates, key
sequences, clipboard contents, titles, URLs, dictionary paths, or raw pointers.
Explicit capture can still reveal activity timing; review data before sharing.

Use the existing provider, permissions flow, and input-access policy. Do not
elevate tested hosts, bypass [restricted input](restricted.md), or add sampling
threads inside the IME. When extending metrics, update protocol/decoder,
aggregation, GUI/export semantics, and focused tests together.
