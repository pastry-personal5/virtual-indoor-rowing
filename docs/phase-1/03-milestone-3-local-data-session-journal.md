# Phase 1 Milestone 3: product `LocalData` session journal + telemetry/capability wire mapping

Status: Implemented (2026-09-19) — automated checks pass; no real-hardware run required
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-18

## Relationship to the delivery plan

This is the third milestone scoped under [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks), per the [Phase 1 packet](README.md)'s "milestones are scoped one at a time" rule. It covers build-order step 4 ("SQLite journal/chunks/recovery/outbox").

Phase 0 Milestone 4 Spike B already built a bounded local-durability diagnostic: `FLocalDataJournalWriter`/`ScanAndRecover` (`Source/LocalData`), a SQLite WAL-mode connection with `schema_migrations`/`journal_events`/`sample_chunks` only, depending on nothing but `RowingCore` telemetry types (`Source/LocalData/Public/LocalData/LocalDataJournal.h:11-17`). Nothing in the repository depends on `LocalData` yet — `Tools/durability-spike` constructs `FSampleChunk` by hand rather than consuming real device telemetry. This milestone's job is to promote that spike into the product local-session journal per `docs/architecture/03-macos-unreal-client.md`'s "Local persistence" section, and to add the telemetry/capability Protobuf wire mapping that Milestone 2 deferred here (its scoping doc: "`LocalData` is the first real consumer, per the same reasoning Milestone 1 used to defer `MetricFrame`").

## Owner-confirmed scoping decisions (2026-09-18)

- **Full 9-table schema now.** `docs/architecture/03-macos-unreal-client.md`'s complete "Minimum local tables" list — `schema_migrations`, `sessions`, `journal_events`, `sample_chunks`, `session_summaries`, `sync_outbox`, `cloud_links`, `installed_content`, `paired_devices` — is stood up in this milestone, not split across several. `sync_outbox`/`cloud_links`/`installed_content`/`paired_devices` will not yet have a real writer (no `Services/` control plane, no content/pairing system exists), so those tables' schemas and idempotent-write contracts land now but are exercised only by tests, not live production code paths — same shape as `LocalData` itself was after Phase 0 Milestone 4 Spike B.
- **AES-256-GCM/Keychain encryption is in scope now.** Fitness summary payloads and compressed sample chunks are encrypted per-blob with a per-local-profile data key held in macOS Keychain (`docs/architecture/03-macos-unreal-client.md`, `docs/architecture/07-security-privacy-safety.md`), using Apple's reviewed cryptographic APIs (CryptoKit) behind a native adapter — never a custom cipher. This is a real scope increase over Spike B, which wrote plaintext.
- **No live capture loop.** This milestone stops at schema + domain↔wire mapping + a `LocalData` write/read API shaped for a real consumer, backed by tests. It does not wire `CreateConcept2PMDiscovery()` telemetry into `FLocalDataJournalWriter` end-to-end — there is no `WorkoutRuntime`/session-orchestration consumer yet to drive that loop correctly (session state transitions, not raw telemetry, decide when a journal write happens), and no Unreal UI (build-order step 5) to observe it. Wiring a live loop ahead of its real caller risks guessing that caller's shape wrong. Live wiring is explicitly future work, most likely alongside or after build-order step 5.

## In scope

1. **Schema promotion.** Extend `LocalData::Private::EnsureSchema()` (`Source/LocalData/Private/Schema.cpp`) to create all 9 tables from `docs/architecture/03-macos-unreal-client.md`, versioned via a new `schema_migrations` row (version 2). Existing Spike B `journal_events`/`sample_chunks` rows/behavior are preserved; `sessions`/`session_summaries` are new. Column-level types follow `docs/architecture/06-data-and-protocols.md`'s units/nullability table (integer fixed-scale SI units, UUIDv7 session IDs stored as 16-byte blobs).
2. **AES-256-GCM encryption.** A native macOS adapter, the new Apple-only `Source/LocalDataMac` module (Apple types stay out of `LocalData`'s public headers per the module-boundary rule), generates/retrieves a per-local-profile data key from Keychain and implements `IBlobCipher`. `sample_chunks.payload_blob` and `session_summaries.metrics_blob` are encrypted before write, decrypted on read; nonces are unique per blob, session/chunk identity is authenticated as associated data. `schema_migrations`/`journal_events`/`sync_outbox`/`cloud_links`/`installed_content`/`paired_devices` stay plaintext (indexing/technical state only, per the architecture doc).
3. **Telemetry/capability wire mapping.** Add `DeviceCapabilityObserved` and `MetricSampled` messages to `Contracts/proto/rowing/v1/` (new file or extension of `session.proto` — decided during implementation), mirroring `FRowingMachineInfo` and `FRowingMetricSample` (`Source/RowingDevice/Public/RowingDevice/RowingMachineTypes.h`, `Source/RowingCore/Public/RowingCore/RowingTelemetry.h`) by field name, not raw enum integer value, matching `SessionStateChangedEvent`'s existing convention. A domain↔wire mapper (new `LocalData` or `RowingContracts`-adjacent code, TBD during implementation) converts `FRowingMetricSample`/`FRowingMachineInfo` to/from these messages for use as `journal_events`/`sample_chunks` payloads.
4. **`FLocalDataJournalWriter` API additions** for `sessions`/`session_summaries` rows: session creation/state update, final-summary write reusing the existing staged-commit two-phase pattern, keyed by `FRowingSessionId` (Milestone 1, `Source/RowingCore/Public/RowingCore/RowingSession.h`).
5. **Tests** (behavioral names), extending `Source/LocalData/Tests/LocalDataJournalTests.cpp` and/or new files:
   - Schema migration from a Spike B v1 database to v2 preserves existing `journal_events`/`sample_chunks` rows, e.g. `local_data_schema_migrates_v1_journal_without_data_loss`.
   - Round-trip encryption: a written chunk/summary is unreadable as plaintext on disk and decrypts correctly via the Keychain-backed key, e.g. `local_data_sample_chunk_payload_is_encrypted_at_rest`.
   - Domain↔wire round-trip: `FRowingMetricSample`/`FRowingMachineInfo` → Protobuf → back is lossless, e.g. `metric_sampled_wire_mapping_round_trips`, `device_capability_observed_wire_mapping_round_trips`.
   - `sessions`/`session_summaries` write/read behavioral tests analogous to existing `local_data_tests` coverage.
6. **Docs:** this checkpoint doc; `docs/phase-1/README.md` milestone list; `docs/phase-1/CHANGELOG.md` entry; `CLAUDE.md`'s `LocalData` line in "Module boundaries" (currently describes only the Spike B subset).

## Out of scope (explicitly, to keep this milestone bounded)

- Any live `RowingDevice`/`Concept2PM` → `LocalData` capture loop — no real writer exists yet for `sync_outbox`/`cloud_links`/`installed_content`/`paired_devices`, and no `WorkoutRuntime` drives `sessions`/`session_summaries` writes from real telemetry. Future work.
- `Services/` control-plane consumption of `sync_outbox` — `Services/` remains `planned`; this milestone only makes `sync_outbox`'s schema and idempotent-write contract exist and pass tests.
- Any Unreal/`RowingUI`/`RowingWorld` consumer of journaled data — build-order step 5, later.
- Structured/managed workout journaling (CSAFE program data) — stays out per Milestone 2's same Phase-2 deferral.
- Cloud key backup/recovery for the Keychain-held data key, and any multi-device key-sharing story — single local profile only, consistent with `docs/architecture/07-security-privacy-safety.md`.

## Owner action required (outside this milestone's code)

None identified yet — this milestone is schema/mapping/encryption work with no external device or hardware dependency, unlike Milestone 2. If a real-hardware or manual verification need emerges during implementation, it will be flagged here before close-out, per the [Phase 1 README](README.md#completion-rule).

## Compatibility

Existing Spike B databases (`schema_migrations` version 1) migrate forward via a new version-2 migration; `Tools/durability-spike`'s existing kill/recover harness and its `durability_spike_kill_recover` CTest continue to exercise the same `journal_events`/`sample_chunks` write path unchanged in behavior. The harness still constructs `FLocalDataJournalWriter` without a cipher, so it writes plaintext `raw-v1` chunks and does not cover the sealed-chunk recovery path; that path is covered only by `local_data_recovery_truncates_corrupt_sealed_chunk_without_a_key`. `AppendChunk`/`RecordJournalEvent`/`StageChunk`/`CommitStagedChunk`/`StageFinalEvent`/`CommitStagedFinalEvent`/`Close`/`ReadSampleChunks`/`ScanAndRecover` keep their existing signatures and semantics, except that `ReadSampleChunks` gains a trailing `bAllowLegacyPlaintext` parameter and, when given a cipher, rejects plaintext chunks unless it is set (see implementation notes); new methods are additive.

## Verification plan for this milestone

- `make build` and `make test` (new `LocalData` schema/encryption/wire-mapping tests, plus regression of existing `local_data_tests`/`durability_spike_kill_recover`).
- `make format-check`.
- `python3 Scripts/check_public_header_dependencies.py` (already run by `make test`) — confirms no Apple/CoreBluetooth/CryptoKit types leak into `LocalData`'s public headers.
- `git diff --check` / `git status --short` before handoff.
- No real-PM5 hardware run required for this milestone (no device interaction).

## Implementation notes (2026-09-19)

Where implementation differs from or settles the scoping text above:

- **CryptoKit via a Swift shim.** CryptoKit's `AES.GCM` is Swift-only and the repo had never compiled Swift. Owner-confirmed: a small Swift file (`Source/LocalDataMac/Private/AesGcmCryptoKit.swift`) exposes `@_cdecl` C-ABI seal/open functions over the already-pinned Xcode `swiftc`; no new external tool. CMake links the Swift library into a C++ adapter and forces `LINKER_LANGUAGE CXX` on `LocalDataMac` and its tests, because a Swift library in the link closure otherwise selects `swiftc` as the driver.
- **`IBlobCipher` only, no `IDataKeyProvider`.** LocalData sees just `IBlobCipher` (`Source/LocalData/Public/LocalData/BlobCipher.h`); the Keychain key is internal to `LocalDataMac`. `LocalData` tests use a fake cipher, so no test outside `local_data_mac_tests` touches Keychain.
- **Sealed layout** is CryptoKit's `combined` form (12-byte random nonce, ciphertext, 16-byte tag). Chunk associated data binds session and sequence range; summary associated data binds session and revision. The chunk CRC32C covers the stored (sealed) bytes, so `ScanAndRecover` needs no key.
- **Wire mapping** is `Contracts/proto/rowing/v1/telemetry.proto` plus a private byte-in/byte-out mapper (`Source/LocalData/Private/TelemetryWireMapping.*`); Protobuf types never appear in a header. `LocalData` therefore now also depends on `RowingDevice` (for `FRowingMachineInfo`) and links `RowingContracts`, which moved out of the tests-only CMake block.
- **`SampleChunkCodec` is retained** as the inner chunk encoding; the Protobuf messages are the domain-to-wire contract but are not yet used as chunk payloads. Retiring the codec is a separate decision.
- **Session identity in the new tables** is the 16-byte `FRowingSessionId` blob; the v1 journal tables keep their TEXT `session_id`, so joins across the two need the canonical-string conversion.
- **Transactions and errors.** Every writer method runs through one transaction-scope helper that rolls back on any exception (best-effort, so an automatic SQLite rollback cannot mask the original error); a failed `Seal`, duplicate key, or failed commit no longer leaves the connection inside an open transaction. Migrations re-check "already applied" under `BEGIN IMMEDIATE`, and connections set a 5 s busy timeout, so concurrent openers of a v1 database migrate it once.
- **Codec is unauthenticated, so readers enforce it.** With a cipher, `ReadSampleChunks` throws on a `raw-v1` row unless `bAllowLegacyPlaintext` is passed (for pre-encryption Spike B data), and always throws on an unknown codec. The CRC32C is a crash-integrity check only and provides no authenticity.
- **Referential integrity.** `session_summaries` and `cloud_links` reference `sessions(session_id)`. The v1 `journal_events`/`sample_chunks` tables key by TEXT and cannot carry the same constraint without a rewrite. `ScanAndRecover` moves an open `sessions` row (matched by canonical id string) to `Ended` in the same transaction that records the `RecoveredAfterUncleanExit` marker, so the two stores agree after a crash.
- **Summary payload is opaque.** `FSessionSummary::MetricsPayload` is caller-encoded bytes; no summary-metrics contract exists yet.
- **Not implemented:** the plaintext-to-sealed migration of existing v1 chunks (Spike B data stays plaintext, marked by the `raw-v1` codec); no writer exists for `sync_outbox`/`cloud_links`/`installed_content`/`paired_devices`.
