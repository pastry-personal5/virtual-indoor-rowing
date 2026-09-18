# Phase 1 Milestone 2: product RowingDevice/Concept2PM capability handshake

Status: Implemented (2026-09-18) — `make build`/`make test`/`make format-check` all pass; real-PM5 confirmation run is outstanding (owner-run, see "Owner action required" below)
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-18

**Scope correction (2026-09-18, before implementation started):** this milestone's original "in scope" item 1/2 below described adding a reviewed non-diagnostic profile and wiring it into the product factory, on the premise that `CreateConcept2PMDiscovery()` (no-arg) resolved to `AdapterPrivateProfiles()` (`DiagnosticOnly = true`). That premise did not match the repository: a governed, generated capability-profile pipeline (`Config/PM5Capabilities.json` → `Scripts/generate_pm5_capabilities.py` → `Concept2PM::GetGeneratedPM5CapabilityProfiles()`) already existed from Phase 0 (commit `dfdd471`, 2026-09-14), already defined a reviewed, non-diagnostic profile (`support_state: "Allowed"`, `DiagnosticOnly = false`, `approved_on: "2026-09-14"`) for the exact PM5/634/8200-000372-178.069 tuple covering `GeneralStatus`/`AdditionalStatus1`/`StrokeData`/`AdditionalStrokeData`, and `CreateConcept2PMDiscovery()` already resolved to it — `AdapterPrivateProfiles()` was only ever reachable via the separate `CreateConcept2PMDiagnosticDiscovery()` diagnostic-path factory. Items 1 and 2 below were therefore already done, predating this milestone's scoping; this milestone's actual implementation work narrowed to item 4 (tests) and item 5 (docs), with item 4's third bullet adjusted per the note there.

## Relationship to the delivery plan

This is the second milestone scoped under [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks), per the [Phase 1 packet](README.md)'s "milestones are scoped one at a time" rule. It covers build-order step 3 ("PM simulator, protocol codec, CoreBluetooth adapter, capability handshake").

Phase 0 already built a substantial diagnostic-grade implementation of this step: `IRowingMachine`/`IRowingMachineDiscovery` (`Source/RowingDevice`), the full `FMacDiscovery`/`FMacMachine` CoreBluetooth adapter with capability-profile matching, connect/disconnect, reconnect, and CSAFE workout program/verify/abort (`Plugins/Concept2PM/Source/Concept2PMMac/Private/Concept2PMDiscoveryMac.mm`), and a PM simulator (`Tools/pm5-sim`). This milestone's job is to promote and close the gaps in that existing foundation for the product path, per the [Phase 1 README](README.md#in-scope-once-milestones-are-scoped)'s "reused from the Phase 0 diagnostic foundation, promoted from diagnostic-only to a stable public contract" framing — not to build a parallel implementation.

## Owner-confirmed scoping decisions (2026-09-18)

- **Promote the existing foundation.** `IRowingMachine`/`FMacDiscovery`/`FMacMachine`/`Tools/pm5-sim` are the product path. This milestone reviews and closes gaps in what exists; it does not stand up a second adapter.
- **Telemetry/capability wire mapping is deferred to Milestone 3.** `FRowingMetricSample`/`DeviceCapabilityObserved`/`MetricSampled` Protobuf messages are not added here — `LocalData` is the first real consumer, per the same reasoning Milestone 1 used to defer `MetricFrame`. This milestone stays in-process-types only, same as `RowingDevice`/`Concept2PM` are today.
- **CSAFE managed workout stays diagnostic-only.** `ProgramDiagnosticWorkout`/`AbortDiagnosticWorkout` (Phase 0 Milestone 4 Spike A) are not promoted to a stable product contract in this milestone. Structured/managed workout is Phase 2 scope per the delivery plan ("Structured time/distance/interval plans and cues; PM managed mode where proven"). Milestone 2 only needs discovery/connect/telemetry for the gray-box walking-skeleton row.

## In scope

1. **Reviewed non-diagnostic capability profile.** Today `CreateConcept2PMDiscovery()` — the no-argument factory `IConcept2PMDiscovery` product callers are meant to use — resolves to `AdapterPrivateProfiles()` (`Concept2PMDiscoveryMac.mm`), whose one profile has `DiagnosticOnly = true`. A connection against that profile is accepted for telemetry but flagged with `ERowingFaultCode::UnsupportedIdentity` / "unverified local diagnostic profile; workout use disabled" (`Concept2PMDiscoveryMac.mm:2266-2270`). This means the product entry point cannot today complete a connection without that warning fault. This milestone adds a reviewed, non-diagnostic profile (`DiagnosticOnly = false`) for the exact PM5/Model D tuple already evidenced on real hardware in Phase 0 Milestones 1-3 (`MonitorModel = "PM5"`, `HardwareRevision = "634"`, `FirmwareRevision = "8200-000372-178.069"`), scoped to the telemetry characteristics already required (`GeneralStatus`, `AdditionalStatus1`) plus whatever additional read/notify telemetry characteristics the walking-skeleton HUD needs. Workout-program characteristics are not part of the reviewed profile — CSAFE control stays reachable only through the existing diagnostic-only path, consistent with the scoping decision above.
2. **Wire the reviewed profile into the product factory.** `CreateConcept2PMDiscovery()` (no-arg) is updated to resolve the new reviewed profile instead of `AdapterPrivateProfiles()`. `CreateConcept2PMDiagnosticDiscovery()` keeps resolving the existing diagnostic-only profile unchanged, so `make pm5-tui`/`make hil-pm5` behavior is not affected.
3. **PM simulator reuse.** `Tools/pm5-sim` (`MockRowingMachine`, `ReplayRowingMachine`, `TelemetryFixtures`) is reused as-is for product-path tests. It is extended only if a capability-handshake scenario isn't already representable — e.g. a fixture for "reviewed profile accepted" versus the existing "capability profile is not approved" rejection path.
4. **Tests** (behavioral names):
   - A reviewed-profile acceptance test against the real, generated product profile set (not a synthetic one): `Concept2PM::EvaluateCapability()` on the PM5/634/8200-000372-178.069 identity against `Concept2PM::GetGeneratedPM5CapabilityProfiles()` returns a non-diagnostic, `Allowed` profile requiring notify on both `GeneralStatus` and `AdditionalStatus1` — the exact condition `FMacMachine::FinishIdentity()` (`Concept2PMDiscoveryMac.mm`) checks before accepting a connection without an `UnsupportedIdentity` fault. Implemented as `concept2pm_reviewed_profile_accepted_no_diagnostic_fault` (`Tests/Contract/pm5_reviewed_profile_tests.cpp`).
   - The rejection path against that same real profile set: an unrecognized firmware revision or machine kind is rejected (`Evaluation.Profile == nullptr`, `SupportState == Blocked`). Implemented as `concept2pm_unreviewed_identity_rejected_against_reviewed_profiles` in the same file. (The synthetic-profile rejection case was already covered by `capability_profiles_and_event_kinds_remain_separate` in `Concept2PMProtocolTests.cpp`; this new test covers the same shape against the real generated profiles instead of a hand-built one.)
   - **Adjusted from the original scoping:** an end-to-end test of `CreateConcept2PMDiscovery()` against `Tools/pm5-sim` fixtures, as originally planned, is not achievable — `FMacDiscovery`/`FMacMachine` (`Concept2PMDiscoveryMac.mm`) are hard-bound to live `CBCentralManager`/`CBPeripheral`, with no swappable transport `pm5-sim` (a wholly separate `IRowingMachine`/`IRowingMachineDiscovery` implementation) can be substituted into. The product entry point's no-hardware behavior is already exercised end-to-end by the existing `pm5_tui_smoke`/`pm5_tui_invalid_selection`/`pm5_tui_ignores_blank_commands` CTest cases (`Tools/pm5-tui`, which calls `CreateConcept2PMDiscovery()` unconditionally in script mode); combined with the profile-level tests above, that is the coverage this milestone relies on instead of a fabricated simulator-backed CoreBluetooth test.
5. **Docs:** this checkpoint doc; `docs/phase-1/README.md` milestone list; `docs/phase-1/CHANGELOG.md` entry.

## Out of scope (explicitly, to keep this milestone bounded)

- CSAFE managed-workout promotion to a product contract — stays diagnostic-only; Phase 2 territory.
- Protobuf wire mapping for telemetry/capability facts (`DeviceCapabilityObserved`, `MetricSampled`/`MetricFrame`) — Milestone 3 (`LocalData`) once there is a real consumer.
- Any `LocalData`, Unreal, or control-API consumer wiring — this milestone only changes what the product discovery/adapter entry point resolves to and adds tests; nothing yet calls `CreateConcept2PMDiscovery()` from a product code path.
- Broadening the PM support matrix beyond the one tuple already evidenced in Phase 0 Milestones 1-3 — that is the risk register's "PM5 firmware/hardware fragmentation" item and stays future work.
- Any change to the diagnostic-only path's own behavior (`make pm5-tui`/`make hil-pm5`), beyond what's needed so it keeps working unchanged after the product factory switches profiles.

## Owner action required (outside this milestone's code)

A reviewed non-diagnostic profile is a capability-handshake behavior change, not pure domain/contract code — unlike Milestone 1, this milestone's completion rule requires real-hardware confirmation per the [Phase 1 README](README.md#completion-rule)'s "where the milestone's scope includes real-hardware behavior, its required real Model D/PM5 hardware run meets the applicable exit criteria." A real-PM5 run via `make pm5-tui` (or `make hil-pm5` if bounded raw evidence is wanted) confirming the reviewed profile connects and streams telemetry with no `UnsupportedIdentity` fault is owner-run, not run in this session — flagging this now so it isn't missed at milestone close-out.

## Compatibility

`AdapterPrivateProfiles()` and the diagnostic factory are unchanged; existing diagnostic tooling keeps working. The new reviewed profile is additive. Once this lands, changes to the reviewed profile's characteristics or `DiagnosticOnly` disposition need the same A0 contract-change review as any other `RowingDevice`/`Concept2PM` public-contract change.

## Verification plan for this milestone

- `make build` and `make test` — done 2026-09-18; new `pm5_reviewed_profile_tests` CTest target passes, all 19 CTest cases pass.
- `make format-check` — done 2026-09-18; clean.
- `python3 Scripts/check_public_header_dependencies.py` (already run by `make test`, as `public_header_dependency_check`) — confirms no Apple/CoreBluetooth type leaks into `RowingDevice`'s public headers.
- `git diff --check` / `git status --short` before handoff.
- Real-PM5 confirmation run (see "Owner action required" above) — owner-executed, not run in this session. This milestone's `Status:` line above does not claim this run has happened.
