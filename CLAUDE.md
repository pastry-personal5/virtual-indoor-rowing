# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Start here

This is an architecture-first repository. Before editing, read `README.md`, `ARCHITECTURE.md`, the affected spec under `docs/architecture/`, and relevant ADRs in `docs/adr/`. Architecture text using **must**/**shall**, and accepted ADRs, is normative; research under `docs/archive/research/` is dated evidence, not requirements. Accepted ADRs change only through a superseding ADR.

The repo ships a native diagnostic build/test system and an Unreal Shipping-toolchain diagnostic host (`VirtualRowing.uproject`) for delivery Phase 0 (`docs/phase-0/README.md`). Phase 0 Milestones 1–3 (native PM5 diagnostic path, TUI relaunch/reconnect, Unreal Shipping toolchain spike) are all complete, and Phase 0 Milestone 4 Spike A (steps 1–3) and Spike B (local durability) are complete. Per [ADR-0010](docs/adr/0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md), the remaining Phase 0 spike work (Spike A step 4 real-PM5 runs, Spike C visual performance, and the realtime spike) is deferred to Phase 4, the revised delivery Phase 0 exit gate is met, and **delivery Phase 1 (walking skeleton) is open**. There is no gameplay, workout, persistence, or PM5 product behavior yet beyond the bounded diagnostic/spike foundation. Milestone completion never implies a delivery-phase exit gate has passed on its own; gate status lives in `docs/phase-0/README.md` and `docs/architecture/10-delivery-plan.md`, not here.

## Commands

All commands run from the repo root via the pinned `Makefile` / `Scripts/dev.py` — don't invoke `cmake`/`ctest`/`clang-format` directly, and don't rely on personal shell setup.

```sh
make doctor          # verify host toolchain against Config/BuildVersions.json (Xcode, cmake, ninja, Unreal 5.8 patch, Git LFS)
make configure        # generate CMake/Ninja build files (arm64 Debug) into Build/native/
make build            # configure if needed, then compile all native diagnostic-client targets
make test             # build, then run CTest with failure output enabled
make format-check     # clang-format --dry-run --Werror over Source/, Plugins/Concept2PM, Tools/, Tests/
make pm5-tui          # build and launch the interactive PM5 diagnostic TUI
make hil-pm5          # same TUI, explicitly enabling the bounded hardware-probe capture (user-driven, real PM5 required)
make pm5-tui-journal  # same TUI with the opt-in Keychain-sealed workout journal (Phase 1 Milestone 4; creates a Keychain item on first run)
make unreal-smoke     # compile the UnrealEditor Development target (only after `make doctor` passes)
make unreal-shipping  # BuildCookRun the unsigned arm64 Shipping diagnostic host via RunUAT.sh
make unreal-package-verify        # inspect the staged Shipping .app only (no sign/notarize/launch)
make toolchain-bluetooth-probe    # run the bounded CoreBluetooth/TCC diagnostic in the staged app
make release-sign-notarize        # protected sign/notarize procedure; requires VIR_DEVELOPER_ID_IDENTITY and VIR_NOTARY_KEYCHAIN_PROFILE env vars, never takes credentials as arguments
make clean            # remove Build/native/ only
make clean-unreal     # remove Unreal-generated intermediates: Saved/, Intermediate/, Binaries/, DerivedDataCache/, Build/unreal-shipping/
make clean-logs       # purge Logs/pm5-tui/*.log*
make clean-metrics    # purge Metrics/pm5-tui/*.jsonl
```

Run a single native test after building (target names come from `CMakeLists.txt`, e.g. `rowing_core_tests`, `pm5_tui_logger_tests`, `pm5_tui_metrics_writer_tests`):

```sh
ctest --test-dir Build/native --output-on-failure -R <test-name-regex>
```

Set `UE_ROOT` if the approved stock Unreal 5.8.2 install isn't in a recognized default location (see `find_unreal` in `Scripts/dev.py`).

For documentation-only changes, verification is:

```sh
rg --files README.md docs
git diff --check
git status --short
```

Before handoff on any change: run `git diff --check` and `git status --short`, then state exactly what was verified and what remains unverified (e.g. simulator-only vs. real-hardware evidence).

## Architecture

Full detail lives in `ARCHITECTURE.md` and the numbered specs under `docs/architecture/`. Summary:

```text
PM5 ──BLE──> Concept2PM adapter ──> rowing domain ──> Unreal UI / world
                                      │       │
                                      │       └──> SQLite journal ──> cloud sync
                                      └──> race client ──WSS──> authoritative race room
```

**Four truths (never collapse these):**

| Truth | Authority | Rule |
|---|---|---|
| Physical rowing facts | PM5 | Never synthesize distance after a gap or reconnect |
| Local workout continuity | Append-only SQLite journal | A network failure cannot discard a completed workout |
| Ranked result | Authoritative race server | Client transforms and Unreal physics never determine rank |
| On-screen motion | Client presentation | May predict/correct; never official |

**Module boundaries** (dependencies flow from platform/framework adapters toward domain contracts, never the reverse; `RowingCore` must build and test without Unreal, CoreBluetooth, a network, or a database runtime):

- `RowingCore` (`Source/RowingCore`): engine-independent C++ domain types and state machines.
- `RowingDevice` (`Source/RowingDevice`): transport-neutral device contracts and telemetry normalization.
- `Concept2PM` (`Plugins/Concept2PM`): Objective-C++/CoreBluetooth and PM protocol adapter — Apple/Concept2 types stop here, never leak upward. Owns discovery, identity, capabilities, subscriptions, decoding, control commands, and reconnect policy; emits normalized ordered facts with source time, sequence, provenance, and quality flags. Only validated device facts affect a local workout; a disconnect freezes official input, records the gap, and attempts bounded reconnect — never fabricates meters.
- `WorkoutRuntime` (`Source/WorkoutRuntime`, Phase 1 Milestone 4, engine-independent): the Just Row session orchestrator — device events → session state machine → `IJournalSink`/`LocalData` journal, gap handling, and a pull `FWorkoutSnapshot` for the UI. Single-threaded and poll-driven; depends on `RowingCore`/`RowingDevice`/`LocalData`, never Apple or Unreal types. `CourseRuntime`, `RaceClient` (planned): local route presentation, online-race client logic.
- `LocalData` (`Source/LocalData`): the local session journal — a SQLite WAL-mode store with the full nine-table schema (`schema_migrations`, `sessions`, `journal_events`, `sample_chunks`, `session_summaries`, `sync_outbox`, `cloud_links`, `installed_content`, `paired_devices`), depending on `RowingCore`/`RowingDevice` domain types and the `IBlobCipher` seam. A private mapper converts telemetry/capability domain types to `Contracts/proto/rowing/v1/telemetry.proto`. `sync_outbox`/`cloud_links`/`installed_content`/`paired_devices` have schema only and no writer yet; there is no live device→journal capture loop. `LocalDataMac` (`Source/LocalDataMac`, Apple-only) implements `IBlobCipher` as AES-256-GCM via CryptoKit (a small Swift shim exposing a C ABI) with the data key held in Keychain; Apple types stop there. Cloud sync remains planned. `OnlineClient` (planned): control-plane access.
- `RowingUI`, `RowingWorld` (`Source/VirtualRowing`, Unreal-side): UMG/CommonUI, actors, rendering, audio, content. Consume domain snapshots only — never parse devices, persist sessions, or determine race results. `UObject`/Actor/Slate/UMG usage is restricted to the game thread.
- `Diagnostics` (`Tools/pm5-tui`, `Tools/pm5-sim`): redacted observability and consented support tooling.

**Persistence and contracts (planned, not yet implemented):** SQLite (WAL) holds the local event journal/outbox/preferences; Keychain holds secrets; PostgreSQL holds durable cloud records; object storage holds immutable compressed sample/replay objects; Redis is ephemeral-only (presence, leases, queues, caches). Queue consumers and finalization paths must be idempotent. Protobuf is canonical for real-time/object events (`lower_snake_case` fields, never reuse field numbers); OpenAPI defines control APIs. Version every externally visible protocol/schema before implementation; keep contracts small and backward compatible for the supported client window. Details: `docs/architecture/06-data-and-protocols.md`.

**Race model:** a race room is a deterministic, single-owner event loop: it validates sequenced metric frames against the ruleset/common clock, publishes snapshots, and emits one durable finalization event. It must never let client prediction become an official result; a failed online race leaves the local workout valid, possibly unranked. See ADR-0003.

## Repository layout

| Path | Status | Purpose |
|---|---|---|
| `Source/` | exists | Unreal (`VirtualRowing`) and engine-independent (`RowingCore`, `RowingDevice`, `LocalData`, `LocalDataMac`, `WorkoutRuntime`) C++ modules |
| `Plugins/Concept2PM/` | exists | CoreBluetooth and PM protocol adapter |
| `Tools/pm5-sim/`, `Tools/pm5-tui/` | exists | Simulator/replay fixtures and the PM5 diagnostic TUI |
| `Tools/durability-spike/` | exists | Phase 0 Milestone 4 Spike B kill/recover harness for the `LocalData` journal |
| `Tests/` | exists | Contract and integration test fixtures |
| `docs/` | exists | Specs (`architecture/`), ADRs (`adr/`), phase milestones (`phase-0/`, `phase-1/`), research (`archive/research/`) |
| `Contracts/proto/` | exists (`rowing/v1/session.proto` from Phase 1 Milestone 1; `rowing/v1/telemetry.proto` from Milestone 3; `rowing/v1/session_summary.proto` from Milestone 4) | Versioned client/cloud contracts |
| `Services/` | planned | Go control plane, race service, workers |
| `Infra/terraform/` | planned | Cloud infrastructure |
| `Content/` | planned | Unreal assets; large assets use Git LFS |

These roots are architecture decisions, not suggestions. Do not create structure outside this table before a milestone in the relevant delivery phase authorizes it.

## Delivery phases, milestones, changelogs

Every delivery phase has one or more milestones, named `Phase <N> Milestone <M>`; a milestone belongs to exactly one phase. Milestone delivery history goes in `docs/phase-<N>/CHANGELOG.md` (one changelog per phase, covering all its milestones, `Unreleased` at top, dated/reverse-chronological, grouped Added/Changed/Fixed/Removed/Security). A changelog entry never changes architecture or implies a phase exit gate passed. Exit-gate criteria live in `docs/architecture/10-delivery-plan.md` and the owning phase's `README.md` — treat those as the source of truth rather than restating gate details here.

## Engineering rules

- Epic C++ conventions: tabs, braces on new lines, `U`/`A`/`F`/`E`/`I`/`T` prefixes. Format Go with `gofmt`.
- `.clang-format` sets `ColumnLimit: 0` — there is no line-length limit. Don't wrap code solely to satisfy a character count; any future formatter/linter must also be configured without a max line length.
- Add tests and traceable evidence for every `FR-*`/`QA-*` change (unit/property, PM5 simulator fixtures, deterministic race replays, hardware-in-loop as applicable). A simulator never replaces required release hardware evidence. Use behavioral test names, e.g. `pm5_reconnect_same_device`, `TestFinalizeSessionIdempotent`.
- Never commit secrets, signing identities, PM serial numbers, production captures, provider credentials, or generated Unreal directories: `Binaries/`, `DerivedDataCache/`, `Intermediate/`, `Saved/`.
- Commit subjects are imperative and scoped, e.g. `docs: clarify PM5 authority`, `feat(device): decode general status`. PRs state intent, affected requirements/ADRs, verification, and risks (UI screenshots, schema rollback notes, performance/PM5 evidence when applicable).

### PM5 TUI logs and metrics

`make pm5-tui` writes `Logs/pm5-tui/pm5-tui.log` (owner-only, DEBUG threshold, rotates at 1 MiB with 3 backups, Git-ignored) and one owner-only JSONL file per launch under `Metrics/pm5-tui/`. Ordinary runs record normalized/aggregate evidence only — no raw payloads. `make hil-pm5` additionally records bounded rowing-service telemetry payloads, UUID short IDs, parser outcomes, per-characteristic sequence numbers, and monotonic receive timestamps, capped at 65 minutes or 100,000 packets; it never captures identity-characteristic payloads, peripheral identifiers, or PM serial numbers. Treat hardware-probe JSONL as private athlete/device evidence — inspect locally, scrub before using as a fixture, never commit or attach the raw file.

`make pm5-tui-journal` (`--journal`) journals rows through `WorkoutRuntime` into a Keychain-sealed SQLite database at `Metrics/pm5-tui/journal/` (owner-only directory, Git-ignored). It holds athlete data: treat it as private evidence, never commit or attach it; logs record aggregate session state only.

`make toolchain-bluetooth-probe` writes one redacted `Saved/Logs/toolchain-bluetooth-probe-*.json` result per run (schema version, source revision, toolchain fingerprint, timestamp, result state, duration) — no raw BLE data. It requires a verified unsigned Shipping `.app` staged by `make unreal-shipping`.

## Useful references

- `docs/architecture/` — numbered specs (00 executive review, 01 product scope … 10 delivery plan), read in numeric order for a detailed decision
- `docs/adr/` — accepted ADRs (0001 platform/toolchain, 0002 PM5 BLE, 0003 authoritative race service, 0004 offline-first journal, 0005 cloud topology, 0006 macOS distribution, 0007 evidence-gated delivery, 0008 defer signing to Phase 4, 0009 defer Bluetooth TCC scenario matrix to Phase 4, 0010 defer remaining Phase 0 spike evidence to Phase 4, 0011 defer design-partner cohort to Phase 4)
- `docs/phase-0/` — current bounded diagnostic milestones, their changelog, and exit-gate status
