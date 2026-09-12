# Verification strategy

Status: Initial reference  
Owner: Quality engineering  
Last reviewed: 2026-09-12

## Quality model

Verification follows risk, not UI screen count. The most dangerous failures are lost/misattributed workout data, incorrect competition results, unsafe update/device parsing, permission/setup failure at the rower, thermal frame collapse, and privacy/authorization exposure.

Every launch requirement (`FR-*`, `QA-*`) has an owner and evidence link in a machine-readable verification matrix. A test result records app build, content/ruleset, PM hardware/firmware, macOS, toolchain, cloud revision, environment, and fixture seed.

## Test layers

| Layer | Runs | Focus |
|---|---|---|
| Pure unit/property | every change | codecs, units, state machines, workout compiler, summaries, rules, idempotency |
| Contract/schema | every change | Protobuf/OpenAPI compatibility, bounds, cross-language fixtures |
| Component | every change | SQLite repository, API module, worker/provider adapter, CoreBluetooth mock |
| Unreal automation | every change/nightly | subsystem lifecycle, UI view models, asset/route validation, Blueprint compile |
| Multi-process integration | main/nightly | API/gateway/race/worker/data services and client harness |
| Simulator end-to-end | nightly | complete pairing/workout/race flows under deterministic device/network faults |
| Hardware-in-loop | nightly/release | real M5 Mac + PM5/Model D variants, BLE and physical comparison |
| Performance/load/soak | nightly/release | frame/thermal, server tick/fanout, database/queue, memory/leaks |
| Security/privacy | every change/release/periodic | static/dependency, fuzz, authz, secret/log checks, threat cases, external review |
| Release acceptance | each candidate | clean install, permissions, sign/notarize/update/rollback, production synthetic |

The pyramid has many pure tests and fewer physical tests, but no simulator result substitutes for release hardware evidence.

## Deterministic fixtures and tools

### PM5 simulator

`Tools/pm5-sim` implements the transport-neutral device interface first, plus a CoreBluetooth peripheral simulator where practical. Scenarios are declarative and seeded:

- advertising/discovery/RSSI and multiple nearby monitors;
- service/characteristic/property/length variants;
- device identity and capability tuples;
- idle, preparing, rowing, rest, pause, finish, terminate, and reset states;
- 1 s/500 ms/250 ms/100 ms rates and callback reordering;
- duplicates, late peer characteristic, jitter, gap, corrupted length, unknown enum;
- PM time/distance regression and valid new-workout reset;
- disconnect/reconnect to same/different identity;
- CSAFE success, malformed response, timeout-before/after application, and unsafe retry;
- low battery and firmware-specific optional data.

Golden packet captures are sanitized and accompanied by source firmware/hardware/test description and expected normalized event stream. Captures never contain a real athlete's identity or heart-rate history.

### Race load client

A headless Go/C++ load tool speaks the exact public real-time contract and can:

- generate physically coherent PM time/distance/speed/stroke sequences;
- replay captured anonymized shapes with deterministic scaling;
- join, ready, synchronize time, race, reconnect, spectate, and finalize;
- inject duplicates, reorder, lag, bursts, clock skew, implausible values, stale tickets, and protocol abuse;
- assert per-client snapshots, acknowledgements, ranks, and final result hashes.

It reports offered load separately from successful valid workload so overload is not hidden.

### Race replay runner

Given ruleset, route metadata, room transitions, and accepted/rejected input candidates, replay produces byte-stable canonical result content and effective order on macOS developer and Linux server architectures. Any intentional rule change creates a new ruleset/version/golden result; it never updates old goldens silently.

### Fault controls

Use a local network proxy/fault layer and injectable clocks/storage/provider adapters. Tests control latency, loss, bandwidth, connection close, DNS/TLS/provider errors, disk-full, SQLite busy/corruption tail, SQS duplicates, process kill, Redis loss, worker lease fencing, and database failover.

Wall and monotonic clocks are injected in domain/server tests. Do not sleep real time to test a 30-minute workout.

## Core invariant tests

These are release-blocking:

1. PM cumulative facts with a valid source produce exactly one ordered local sample sequence.
2. No device/render/network callback writes Unreal objects off the game thread.
3. No missing network/provider response prevents local session finalization.
4. Killing the app at every journal write boundary loses at most the allowed tail and marks recovery.
5. A PM reset, identity swap, or incompatible reconnect never adds a huge synthetic distance.
6. Duplicate session upload/API request/job/webhook produces one logical outcome.
7. A ranked result uses only accepted metric frames after the common start and retains reject/penalty evidence.
8. Client avatar coordinates cannot influence server progress/rank.
9. Race replay is deterministic across repeat, process, and supported architecture.
10. A stale worker generation cannot publish after a newer room lease fences it.
11. Integration failure/retry cannot duplicate a VIR session or block its completion.
12. Revoked/private data cannot be fetched through guessed IDs, friendships, spectator tickets, or stale URLs.
13. Logs/support bundles contain no known fixture token, email, raw serial, or raw heart-rate sequence.
14. Unsigned/wrong-hash/incompatible content and application updates fail before activation.
15. Subscription/update prompts cannot interrupt an active session.

## Client unit and property tests

### Protocol parsing

- Every allowed characteristic layout and every byte truncation point.
- Minimum/maximum values, sentinels, unknown enumerations, unaligned buffers.
- Little-endian decode on architecture-independent fixtures.
- CSAFE frame start/stop, stuffing/unstuffing, checksum, maximum stuffed/unstuffed size, multiple complete commands, illegal partial command.
- Property-based encode/decode round trips for the supported command subset.
- Coverage-guided fuzzing for BLE and CSAFE entry points with allocation/time limits and sanitizer builds.

### Telemetry and units

- Cross-characteristic merge in all callback orders and at merge-window boundaries.
- Exact 0.1 m→100 mm and centisecond→10 ms conversions without floating drift.
- Optional/sentinel handling; a missing value never becomes zero.
- 24-bit boundaries, reset recognition, regression/gap flags, and duplicate handling.
- Summary/interval totals agree with source resolution.
- Long-duration counters do not overflow target representations.

### Workout/session

- Every legal/illegal session transition.
- Plan schema bounds, repetitions, PM parameter limits, and unsupported fallback behavior.
- Command timeout at each step, readback mismatch, compensation, and safe abort.
- Interval cues at monotonic boundaries despite frame hitch.
- Quit/suspend/disconnect/reconnect at every state.
- Completed/interrupted/aborted remain distinct through local and cloud projections.

### Presentation

- Distance-to-spline transforms at start/end/checkpoints and closed-route wrap.
- Prediction cap, server correction damping, stale coast limit, remote interpolation buffer.
- Boat collision/wake/VFX cannot mutate official distance.
- Avatar phase mapping for every stroke state and fallback.

## SQLite durability tests

Run against real SQLite and filesystem semantics:

- create/migrate every supported prior local schema to current;
- terminate process before/during/after each transaction/fsync boundary;
- partial/corrupt last chunk vs corrupt middle chunk handling;
- WAL recovery, busy timeout, lock contention, disk full, permission/read-only, low storage;
- summary transaction and outbox insertion are atomic;
- account logout/user switch never attaches one user's session to another;
- guest claim is retry-safe and conflict-visible;
- cloud acknowledgement survives crash without deleting unsent work;
- export reads a stable snapshot while a different session is active.

Recovery tests assert exact event/sample loss and quality flags, not only “app opens.”

## Cloud tests

### API and authorization

- OpenAPI request/response/error contract and content types.
- Owner/unrelated/blocked/friend/spectator/operator/service-role negative matrix.
- JWT issuer/audience/algorithm/time/key-rotation and malformed-token cases.
- PKCE/state/nonce/callback and account-linking conflict cases.
- Idempotency key replay, conflict digest, concurrency, retention expiry.
- Pagination stability and bounded request/list/upload sizes.
- SQL injection/SSRF/open redirect/object-key traversal/deserialization cases.

### Storage and asynchronous work

- Object upload scope, size, digest, checksum, expansion/nesting bounds, quarantine.
- Transactional outbox crash before/after commit and relay publish.
- SQS duplicate/out-of-order/visibility timeout/poison/DLQ/redrive.
- Integration timeout/429/5xx/401/403/409/422 and token refresh races.
- Billing webhook signature, ordering, replay, correction/refund, and entitlement revision.
- Export/deletion workflows across every owned table/object/cache/provider.
- Expand/migrate/contract against previous running application revision.

### Race service

- Phase/message allow-list and per-phase quotas.
- Single-use ticket, wrong room/role/audience/build, expiry and reconnect grant.
- Time-sync offset/uncertainty, false-start boundaries, delayed packets.
- Accepted/rejected distance at exact ruleset boundaries.
- Input/snapshot sequence wrap assumptions and duplicate ack.
- Finish ties, DNF/DNS, penalties, adjudication revisions.
- Gateway drain, worker failure, lease fencing, checkpoint/retransmit/recovery.
- Finalization job loss/duplicate and result transaction idempotence.
- A restarted/recovered room yields expected generation and audit hash.

## Unreal functional and content tests

- All maps load in Editor and Shipping cook with no missing redirector/reference.
- Route validator checks spline continuity, authored length, checkpoints, lane bounds, spawn, metadata hash.
- Every published workout/route/menu is reachable, localized, and has fallback text/assets.
- Blueprint compile has zero errors and no disallowed domain/security nodes.
- Input navigation works mouse, keyboard-only, gamepad if claimed, and after focus loss.
- Text scale/high contrast/color-deficiency/motion settings keep critical HUD and countdown usable.
- Audio/subtitle/redundant cue equivalence and no critical cue depends only on color/audio.
- Safe built-in route starts if downloaded content is absent/corrupt.
- World travel, reconnect, account refresh, content check, and history query do not leak references over repeated sessions.

Use Unreal Automation and Gauntlet for packaged multi-instance flows where applicable; custom Go race servers remain part of the harness rather than pretending to be Unreal dedicated servers.

## Hardware compatibility matrix

Launch release evidence covers:

| Axis | Required cases |
|---|---|
| Mac | reference M5 Max/128 GB, clean standard user, low storage, external/retina display modes |
| macOS | minimum 26.6.2 and latest approved patch; upgrade from prior approved patch |
| PM5 | each allowed hardware generation; oldest allowed/latest firmware; one blocked/unknown tuple |
| Model D | representative older retrofit and later standard-PM5 machine where available |
| Bluetooth | off/on, first permission allow/deny/repair, other app occupying PM, interference/range, sleep/wake |
| Network | offline, Wi-Fi→offline→Wi-Fi, high latency/jitter/loss, captive/no-internet shape |
| Account | guest, new, returning, expired/revoked token, user switch, deletion pending |
| Install | fresh DMG, app translocation/read-only launch, `/Applications`, update last stable, rollback recovery |

The support matrix is a signed artifact derived from evidence. “Works on my PM5” is not sufficient.

## End-to-end acceptance scenarios

At minimum:

1. Fresh user denies Bluetooth, receives useful repair guidance, authorizes later, pairs the intended PM among two candidates, completes and exports a 2 km row offline.
2. Guest completes a workout, creates an account later, claims it exactly once, and sees the same summary after restart.
3. Managed fixed-time, fixed-distance, and interval plans program/read back the PM and agree with the PM display/log within resolution.
4. Internet disappears for 20 minutes of a one-hour solo workout; local completion is immediate and sync later is idempotent.
5. Renderer is deliberately stalled while PM events continue; HUD catches up without losing state/stroke/completion events.
6. App is killed at sampled persistence boundaries; recovery is explicit and no session is silently shown verified/completed.
7. Two clients with controlled latency run a ranked race; common start, progress, finish order, local summaries, and final result replay agree.
8. Ranked participant disconnects/reconnects within and outside grace; eligibility and local preservation match the ruleset.
9. Race worker dies under load; generation fencing/recovery or cancellation is correct, with no split-brain result.
10. Concept2 endpoint is slow/failing/duplicates; VIR completes normally and delivery status/retry/user action is correct.
11. A private session/ghost/export URL is probed by another account and blocked with an auditable generic response.
12. A signed update from last stable succeeds; wrong signature/hash/feed fails; interrupted update leaves last stable runnable.

## Performance and thermal testing

### Client workload profiles

- Empty safe route baseline.
- Representative route at maximum launch visual complexity.
- Maximum nearby remote boats/avatars/HUD rank list.
- Content download/catalog refresh in menu only; never use active-session results to excuse background work.
- 60-minute steady row and 20-minute high-stroke-rate interval/race.
- Repeated route travel/session start/end for memory growth.

Capture frame, game/render/RHI/GPU time, hitches, shader/PSO compilation, memory pools, thermals, power, BLE-to-HUD latency, queue depth, database latency, network time, and Unreal Insights traces. Warm caches and first-run shader experience are reported separately.

Release passes `QA-001`/`QA-002` on the reference hardware with the display/scalability definition frozen. No benchmark process gets privileged cooling or background conditions unlike the user target.

### Server load

- At least twice forecast and the initial envelope of 5,000 WSS connections/1,000 active 10 Hz rowers.
- Mix lobby, active, spectator, reconnect, finish burst, history/API, and worker jobs.
- One-AZ/task loss, rolling deploy, Redis failover, database failover, SQS backlog, slow external provider.
- Track p50/p95/p99/max tick work and schedule delay, snapshot latency/drop, gateway memory/connection, gRPC stream backpressure, Redis/DB/queue, result lag, and cost.
- A test is invalid if generators cannot sustain offered load or assertions are sampled away.

Soak long enough to expose connection/map/goroutine/file-descriptor/allocator leaks, initially eight hours for public beta and a 24-hour pre-GA online soak.

## Security and privacy verification

- Static analysis, dependency/vulnerability/license scan, secret scan, SBOM and container/image policy each change.
- Continuous fuzz targets for BLE/CSAFE, Protobuf/container, API decoding, FIT generation, and content manifest.
- Automated log/support-bundle canaries assert secrets/PII/heart-rate are absent.
- Authorization regression suite is release-blocking.
- Signed/notarized bundle/entitlement/library verification on final bytes.
- Update threat tests include compromised CDN bytes/feed, replay/downgrade, key rotation, interrupted delta/full fallback.
- Data export completeness and deletion propagation test on a synthetic account, including object/cache/provider mapping.
- Restore test ensures deleted data in isolated backup remains access-controlled and expires under policy.
- External penetration test before GA and after material identity/payment/admin/realtime changes.
- Focused native parser review/fuzz assessment before hardware support expands.

Findings have severity, owner, remediation date, and release policy. “Accepted risk” names the accountable executive and expiry/review date.

## Release evidence and go/no-go

The release candidate produces one evidence bundle:

- source/build/content/contracts/capability versions and artifact hashes;
- CI/unit/contract/fuzz results and coverage trend;
- hardware matrix and end-to-end scenario results;
- performance/thermal and server load/soak reports;
- security/privacy/dependency/SBOM findings and waivers;
- codesign/entitlement/notarization/Gatekeeper/update/rollback evidence;
- database migration/rollback and backup-restore evidence;
- SLO dashboards, alerts, runbooks, on-call readiness;
- known issues, customer copy/support content, and kill-switch validation.

Product, client, online, QA, security/privacy, content, operations, and support owners sign the go/no-go. A waiver states failed requirement, observed evidence, user impact, mitigation, owner, expiry, and rollback trigger. There are no verbal waivers for session loss, result corruption, authorization bypass, signing/update failure, or a physical-safety blocker.
