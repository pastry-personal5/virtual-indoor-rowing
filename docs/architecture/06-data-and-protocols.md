# Data model and protocols

Status: Initial reference  
Owner: Data and API engineering  
Last reviewed: 2026-09-12

## Design rules

1. Keep PM facts, application-derived values, and server decisions distinguishable by provenance.
2. Use integer fixed-scale SI units on wires and in durable records; floating point is presentation/simulation only.
3. Use monotonic clocks for ordering/duration, UTC instants for correlation, and IANA timezone IDs for user-facing calendar meaning.
4. Append facts and create revisions; do not mutate history invisibly.
5. Dense samples live in checksummed compressed objects, not millions of primary-database rows.
6. Every public contract and stored payload declares a version.
7. Every at-least-once operation is idempotent by construction.
8. Unknown optional fields are tolerated; unknown state transitions or incompatible major versions fail closed.

## Canonical identifiers

Identifiers are opaque 128-bit values serialized as 16 bytes in Protobuf and canonical lowercase strings in JSON. UUIDv7 is preferred for new server-generated records because it is sortable without exposing a user sequence. Client-created session IDs may be UUIDv7 generated locally.

| ID | Created by | Scope |
|---|---|---|
| `user_id` | control plane | internal account, never derived from IdP/email |
| `session_id` | client | one physical workout attempt across local/cloud/race |
| `operation_id` | caller | one idempotent mutation/export attempt |
| `device_pseudonym` | server | keyed, non-reversible correlation; not raw PM serial |
| `plan_id`, `route_id` | content system | stable logical definition; version is separate |
| `race_id`, `room_id` | control plane | durable event vs ephemeral live room |
| `result_id` | result service | participant outcome across immutable revisions |
| `message_id`, `event_id` | producer | deduplication/audit |

Display names, email addresses, Bluetooth identifiers, and PM serial numbers are never identifiers in domain relationships.

## Units and nullability

| Quantity | Canonical representation | Notes |
|---|---|---|
| Distance | `uint64 distance_mm` | PM 0.1 m is converted exactly to 100 mm increments |
| Duration/elapsed | `uint64 *_ms` | PM centiseconds convert to 10 ms; no wall-clock subtraction |
| Speed | `uint32 speed_mm_per_s` | optional |
| Pace | `uint32 pace_ms_per_500m` | optional; Concept2 source preferred |
| Power | `uint32 power_w` | distinguish stroke/current/average in field name |
| Stroke rate | `uint32 stroke_rate_deci_spm` | PM integer SPM becomes ×10 |
| Heart rate | `optional uint32 heart_rate_bpm` | sentinel is converted to absent at adapter |
| Drag factor | `optional uint32 drag_factor` | dimensionless source value |
| Energy | `uint32 calories` | source estimate with provenance; no medical precision claim |
| Force | `uint32 force_deci_lb` at adapter, optional derived N | preserve published source resolution |
| Absolute instant | Protobuf `Timestamp` / RFC 3339 UTC | audit and calendar anchor only |
| Timezone | IANA string, for example `Asia/Seoul` | store the zone used at workout start |

Optional means absent, not zero. JSON APIs do not encode 64-bit counters as IEEE-754 numbers when a browser could lose precision; use decimal strings or a bounded documented range.

## Provenance and quality

Every sample carries:

- `source_kind`: `CONCEPT2_PM5_BLE`, later transports get new values;
- adapter sequence and PM elapsed time;
- source characteristic/field revision in raw audit metadata;
- `received_monotonic_ns` locally;
- capability profile and parser version at the session header;
- a `quality_flags` bitset.

Initial quality flags:

| Flag | Meaning |
|---|---|
| `MISSING_FIELD` | expected optional peer characteristic missed merge window |
| `SOURCE_GAP` | notification/source sequence or elapsed cadence has a gap |
| `DUPLICATE` | exact duplicate observed and deduplicated |
| `TIME_REGRESSION` | PM elapsed moved backward outside a valid transition |
| `DISTANCE_REGRESSION` | cumulative distance moved backward outside a valid transition |
| `DEVICE_RECONNECTED` | sample follows a reconciled reconnect |
| `RECOVERED_PROCESS` | session journal recovered after unclean app exit |
| `APP_GUIDED_WORKOUT` | PM was not programmed with the full plan |
| `LATE_CORRECTION` | field arrived after the initial merge publication |
| `UNSUPPORTED_VALUE` | an enum/layout value was structurally valid but unknown |
| `OUTLIER` | value failed plausibility policy and was excluded from ranked input |
| `SERVER_RECOVERED` | race continued after worker generation recovery |

Flags accumulate into interval/session quality without erasing the event that caused them. `verified` is not a Boolean replacement for this evidence; eligibility is a versioned policy decision over evidence.

## Core domain records

### Workout plan

```text
WorkoutPlan
  plan_id, version, title_key, sport=INDOOR_ROW
  steps[]
    step_id
    phase: WARMUP | WORK | REST | COOLDOWN
    duration: exactly one of time_ms | distance_mm | calories | watt_minutes
    target: optional pace | power | stroke_rate | heart_rate_zone | free
    cue_policy
  repeat structure (bounded and expanded before PM compilation)
  source/author and revision metadata
```

Plans are immutable after publication. Editing creates a version. A session embeds the resolved plan snapshot/digest so history does not change when the catalog does.

### Route definition

```text
RouteDefinition
  route_id, version, content_set_id
  course_length_mm
  checkpoint_distance_mm[]
  lane_count and presentation metadata
  route_metadata_sha256
  compatible_client_range
  authored coordinate-system/version
```

Race rules reference the route ID, version, and hash. World coordinates are never sent as official progress.

### Session

```text
Session
  session_id, user_id? / guest_scope
  disposition: COMPLETED | INTERRUPTED | ABORTED
  source/capability/parser metadata
  plan snapshot/hash, route version/hash, race_id?
  start/end UTC, timezone, monotonic duration
  PM baseline/final elapsed and distance
  summary + intervals
  quality flags/evidence references
  sample object digest/pointer?
  local/cloud revision and integration status
```

A sessioned plan's target is not measurement. Store target values separately from actual PM values.

### Race result

```text
RaceResultRevision
  result_id, revision, race_id, participant_id
  ruleset version/hash, content version/hash
  start epoch, accepted finish epoch
  accepted distance/time, rank, outcome, penalties
  last accepted input sequence
  accepted input-chain hash
  integrity status and evidence codes
  room worker generation(s)
  previous_revision_hash
  created_at, actor/reason
```

Adjudication appends a revision. A projection identifies the currently effective revision.

## Local journal event catalog

Event payloads are Protobuf, even inside SQLite blobs, and carry `event_schema_version`.

- `SessionCreated`
- `DeviceSelected`
- `DeviceCapabilityObserved`
- `WorkoutConfigurationStarted`
- `WorkoutConfigurationVerified`
- `SessionStateChanged`
- `ConnectionLost`
- `ConnectionRestored`
- `MetricSampled`
- `MetricCorrected`
- `StrokeCompleted` (PM-keyed sparse stroke facts; may include timing, force, work, power, and calories independently of the sample cadence)
- `IntervalStarted`
- `IntervalCompleted`
- `RaceJoined`
- `RaceStartScheduled`
- `RaceSnapshotObserved` (summary/audit, not every remote transform)
- `RacePenaltyObserved`
- `LocalFinishObserved`
- `SessionInterrupted`
- `SessionSummaryCalculated`
- `SessionFinalized`
- `CloudUploadAcknowledged`
- `IntegrationStatusObserved`

Events are ordered by per-session `uint64 sequence`. The unique constraint is `(session_id, sequence)`. A payload CRC protects each chunk; the session footer includes SHA-256 over ordered chunk digests.

## Session object format

Use a versioned Protobuf container compressed with Zstandard:

```text
SessionObject v1
  Header
    magic, container_version, schema_version
    session metadata and provenance
    compression parameters
  Chunk[]
    first/last sequence
    uncompressed_size, compressed_size
    crc32c(uncompressed payload)
    zstd protobuf event batch
  Footer
    event_count
    summary snapshot
    sha256(ordered chunk metadata + uncompressed chunk digests)
```

Limits are enforced before allocation: total compressed/uncompressed size, expansion ratio, event count, chunk size, string length, repeated-field count, and nesting. Workers stream decode and quarantine invalid objects. CRC detects accidental chunk corruption; SHA-256 binds the object digest; neither proves a malicious client truthful.

Retention can remove the dense sample object while retaining a consented aggregate. The summary stores whether its source object remains available.

## Realtime envelope

Conceptual Protobuf shape (field numbers are assigned only in `Contracts/proto`):

```protobuf
message RealtimeEnvelope {
  uint32 protocol_major = ...;
  uint32 protocol_minor = ...;
  bytes message_id = ...;          // 16-byte UUID
  bytes room_id = ...;
  uint64 sequence = ...;           // per direction/connection
  uint64 ack_sequence = ...;
  uint64 sent_monotonic_ns = ...;  // meaningful for deltas at sender
  oneof payload {
    ClientHello client_hello = ...;
    TimeProbe time_probe = ...;
    MetricFrame metric = ...;
    RaceSnapshot snapshot = ...;
    RaceTransition transition = ...;
    ProtocolError error = ...;
  }
}
```

The authenticated connection supplies participant identity/role; the client does not choose another participant ID inside metric payloads.

### Metric frame

```text
MetricFrame
  session_id
  source_sequence
  pm_elapsed_ms
  cumulative_distance_mm
  speed_mm_per_s?
  stroke_rate_deci_spm?
  stroke_power_w?
  heart_rate_bpm? (only if race rules explicitly require/permit; normally omitted)
  workout_state, rowing_state, stroke_state
  quality_flags
  prior_frame_chain_hash
```

`prior_frame_chain_hash` makes accidental truncation/reordering and server audit easier. It is not claimed as anti-tamper proof because the client holds all inputs needed to recompute it.

### Snapshot

```text
RaceSnapshot
  race_epoch, phase
  worker_monotonic_ns, race_elapsed_ms, UTC audit anchor on transitions
  ruleset_version/hash
  recipient_last_accepted_source_sequence
  participant summaries[]: id, accepted_distance_mm, rank, status, penalty
  nearby presentation states[]: id, distance_mm, lane, filtered_speed
  snapshot_generation and sequence
```

Distance and rank are authoritative. Lane and filtered speed are presentation hints. Snapshot lists are bounded and delta encoding is introduced only after measuring need.

### Compatibility

- Server supports the current protocol major and at least the previous major during a published update window.
- Unknown Protobuf fields are preserved/ignored according to generated runtime behavior.
- A minor version may add optional messages/fields. Capability negotiation disables unrecognized additions.
- A major version changes required semantics and is rejected with a minimum-compatible-client response before a race.
- Every race pins one protocol/ruleset/content combination from lobby through finalization.

## REST conventions

- HTTPS JSON with explicit `/v1` prefix and UTF-8.
- RFC 3339 UTC timestamps and IANA timezones.
- `application/problem+json` errors with stable machine code, human localization key, trace ID, and field details.
- Cursor pagination with stable sort; no unbounded list endpoint.
- `If-Match`/ETag for editable resources.
- `Idempotency-Key` for creation/finalization/export and webhook event ID for providers.
- Request and response size/time limits at edge and application.
- No secret, token, or raw fitness samples in a URL/query string.
- Deprecation and sunset dates are observable in headers/catalog and product release notes.

## PostgreSQL logical model

| Module | Principal tables |
|---|---|
| Accounts | `users`, `identity_subjects`, `profiles`, `user_settings` |
| Privacy | `consent_events`, `data_export_requests`, `deletion_requests` |
| Sessions | `sessions`, `session_revisions`, `session_intervals`, `session_objects`, `device_pseudonyms` |
| Catalog | `plans`, `plan_versions`, `routes`, `route_versions`, `content_sets`, `rollouts` |
| Social | `relationships`, `blocks`, `presence_preferences` |
| Events | `events`, `registrations`, `match_requests`, `races`, `race_participants` |
| Results | `result_revisions`, `effective_results`, `leaderboard_entries`, `integrity_evidence` |
| Commerce | `products`, `entitlement_revisions`, `billing_webhook_events` |
| Integrations | `integration_connections`, `secret_envelopes`, `export_jobs`, `delivery_attempts` |
| Platform | `idempotency_records`, `transactional_outbox`, `operator_audit_events` |

Row-level access is implemented in application authorization, with PostgreSQL roles preventing a deployable from writing unrelated module tables. Migrations are forward-only, reviewed, lock-time bounded, and compatible with both old and new application versions during rolling deployment.

Dense samples, full ghosts, and race accepted-frame logs are referenced by content-addressed S3 objects. Database pointers include bucket class, object key, schema, size, digest, encryption key ID, and retention state.

## Calculations

- Prefer PM-supplied distance, elapsed time, pace, power, calories, stroke count, and interval summaries when valid.
- If a UI must derive current pace from valid speed, use `pace_ms_per_500m = 500000000 / speed_mm_per_s`, label provenance as derived, handle zero as unavailable, and use checked integer arithmetic.
- Official average pace/duration comes from accepted PM/race totals, not the mean of displayed current pace samples.
- Do not reconstruct official distance by integrating a rounded speed while canonical cumulative distance exists.
- Do not infer continuous power by holding the most recent per-stroke power across recovery.
- Calories remain the PM's estimate with its semantics. Body-weight adjustments, if ever shown, are a separate derived estimate.
- Training-zone calculations store the input method/value, algorithm version, and boundaries used. Updating an FTP or max HR does not rewrite past target zones.
- All leaderboard equality/tie/penalty behavior is defined by a versioned race ruleset and replay-tested; it is never delegated to UI sort order.

## Ghost/replay format

A ghost is generated server-side or locally from accepted session samples into a privacy-filtered, resampled artifact:

```text
Ghost v1
  ghost_id, owner visibility/pseudonym
  source session/result revision
  route/ruleset version/hash
  start offset
  frames[]: elapsed_ms, distance_mm, filtered_speed, stroke_phase?
  quality/eligibility
  generator version and artifact digest
```

It contains no email, raw PM serial, access token, GPS location, or heart rate. Revoking visibility removes catalog access and schedules shared artifact deletion according to retention policy.

## FIT export

FIT export is an adapter derived from the canonical completed session, not a second session store. Use a pinned and license-reviewed Garmin FIT SDK/profile. Emit an Activity file with ordered timestamps, session/lap/record/event messages appropriate to indoor rowing, source/device metadata that does not expose a raw serial, and valid header/CRC. Golden files are decoded by an independent compatible reader in CI. A profile upgrade requires compatibility fixtures because FIT consumers may interpret optional fields differently.

## Analytics boundary

Operational telemetry and product analytics are separate streams/purposes.

- Operational data answers whether the system works: failures, latencies, versions, resource health.
- Product analytics answers consented aggregate behavior: onboarding completion, mode use, retention funnels.
- Workout samples and heart rate do not enter general product analytics.
- Analytics events have a reviewed catalog, owner, purpose, fields, retention, and data classification.
- Stable user correlation is avoided unless the defined analysis needs it and consent/legal basis permits it.

Initial analytical storage is partitioned Parquet in S3 queried through Athena, populated from approved domain events. PostgreSQL production replicas are not an analyst workspace.

## Schema evolution checklist

Before merging a contract/schema change:

1. Classify additive, semantic, or breaking change.
2. Define old-client/new-server and new-client/old-server behavior.
3. Add golden serialization and unknown-field tests.
4. Bound size/cardinality and update abuse limits.
5. Update privacy classification and retention where fields change.
6. Provide expand/backfill/contract steps for SQL.
7. Verify rolling deploy and rollback with both application revisions.
8. Update replay determinism and result hash rules if relevant.
9. Record minimum client/content/capability changes in catalog.
