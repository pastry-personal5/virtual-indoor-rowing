# Phase 1 Milestone 7: real PM5 wiring in the Unreal app

Status: Implemented (2026-09-19) — automated checks pass (CTest, Editor-target link, headless Automation spec); simulator, fake-adapter and unit evidence only. The owner-run real-PM5 checks in "Owner-run hardware evidence" are outstanding, and nothing here claims real-hardware behavior
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-19

## Relationship to the delivery plan

This is the seventh milestone under [delivery Phase 1](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks), scoped per the [Phase 1 packet](README.md)'s "milestones are scoped one at a time" rule. It is the "real-device follow-up" that [Milestone 5](05-milestone-5-unreal-hud.md) split out and [Milestone 6](06-milestone-6-hardening-and-refactor.md) cleared the way for. It completes the CoreBluetooth adapter's use in the product app and produces the first real-hardware evidence toward these Phase 1 exit-gate items:

- **FR-002** (discovery, explicit pairing, reconnect), **FR-003** (live HUD) and **FR-007** (durable local history, crash recovery) on a real Model D/PM5.
- The 6-minute hardware run's preliminary latency and durability, and process-kill recovery evidence.

It does **not** deliver **FR-004** (Just Row: "select a downloaded route and row without an account or network"). That needs route content and the route/boat milestone, so the gate's "FR-002/003/004/007 on real hardware" stays open for FR-004 after this milestone. Completing this milestone records progress only; the exit gate is evaluated separately.

### Starting state (verified against the code)

- The packaged app links `RowingCore`, `RowingDevice`, `RowingContracts`, `LocalData`, `WorkoutRuntime`, `RowingSim` and static protobuf/abseil. `Concept2PMCore`, `Concept2PMMac`, `LocalDataMac` and its Swift CryptoKit shim are CMake-only, and CMake builds them only outside the `VIR_APP_LIBS_ONLY` tree that produces `libVirRowingApp.a` (`CMakeLists.txt`, lines guarding `NOT VIR_APP_LIBS_ONLY`; `VIR_APP_LIB_ROOTS` is `WorkoutRuntime` + `RowingSim`).
- `CreateConcept2PMDiscovery()` constructs the `CBCentralManager` in the discovery constructor. So **creating the discovery is what triggers the Bluetooth permission prompt**, and on `PoweredOn` with no scan requested the adapter **automatically tries to reconnect to the remembered peripheral** (`TryReconnectRememberedPeripheral`). Remembered-peripheral state lives in `NSUserDefaults` (peripheral UUID plus identity). Machines the discovery creates hold a reference back to it, so the discovery must outlive every machine.
- The adapter is internally synchronized (mutex-guarded `TryPollEvent`, its own dispatch queues), and its event timestamps and the subsystem's clock both use `std::chrono::steady_clock`.
- `pm5-tui --journal` already runs the same stack against a real PM5 (`FWorkoutJournalDriver`: `ScanAndRecover`, Keychain key, `FLocalDataJournalWriter`, `FLocalDataJournalSink`, `FWorkoutSession`) but its real-PM5 run and Keychain creation from the TUI bundle are unverified (Milestone 2 and 4 confirmations are still outstanding).
- `FWorkoutSessionConfig` defaults flush buffered samples every 5 s or 50 samples. QA-003 requires a checkpoint within 1 s.
- The Shipping `Info.plist` comes from `Build/Mac/Resources/Info.Template.plist`, and `make unreal-package-verify` fails unless the string equals `BLUETOOTH_USAGE_DESCRIPTION` in `Scripts/vir_dev/packaging.py` (also used by `Scripts/test_unreal_packaging.py`).

## Prerequisite (owner-run, before the real path is called wired)

Run `make pm5-tui-journal` against a real PM5 for a short row, then inspect the journal and confirm the Keychain item was created. This closes the outstanding Milestone 2 and 4 hardware confirmations and de-risks three things this milestone would otherwise discover inside the app: the adapter against real firmware, whether a real Just Row ends in `Complete`/`Terminated`, and Keychain behavior in an ad-hoc bundle. The link spike and fake-adapter work do not need it; the "real path wired" claim does. If it surfaces adapter defects, they are fixed in a preceding hardening commit, not in the app wiring.

## Proposed scope decisions (owner may veto)

**Connection flow (matches how the adapter really behaves)**
- **One "Connect to PM5" action.** No Bluetooth object exists until the user clicks it, so a normal launch never prompts (consistent with ADR-0009's normal-launch-no-prompt row, which is only recorded here, not certified). The click creates the discovery via `CreateConcept2PMDiscovery()` (the reviewed profile set; never the diagnostic or hardware-probe factories).
- **Remembered PM5 is implicit.** The adapter starts a reconnect to the remembered PM5 by itself once Bluetooth is `PoweredOn`. The UI therefore shows "Reconnecting to your last PM5…" with a visible "Scan for a different PM5" action, and falls to the scan list on the adapter's "remembered PM5 unavailable" fault or a bounded timeout. The app cannot know whether a remembered PM5 exists without creating the central, so it has no separate "Reconnect" button. "Forget PM5" calls `ForgetRememberedMachine()` and disconnects any machine already transferred.
- **Picker.** Nearest-first list labelled by index and dBm ("PM5 #1 (-52 dBm)"), as already owner-confirmed in Milestone 5; no privacy change. Code-only UMG, like the HUD. Selecting an entry is the explicit pairing step for FR-002.
- **Machine ownership.** The subsystem owns discovery, then machine, then session, and tears down in reverse (session, machine, discovery). A relaunch machine is taken with `TryTakeRelaunchMachine()` and handed to exactly one owner. One poller: the subsystem drains the machine and ingests each event with `bPollMachine = false`, the Milestone 6 contract.
- **Device panel states** (each with actionable text and a way out): idle, requesting Bluetooth, Bluetooth off, Bluetooth denied (points to System Settings; retry), scanning (bounded, then "no PM5 found"), candidates, connecting, unsupported firmware/identity (the adapter's `Unsupported` state; show the reason category, not raw identifiers), connected, reconnecting. No state is a dead end and none hangs.

**Journaling (policy already confirmed: on for real devices, off for the simulator)**
- Real-device sessions journal through `FLocalDataJournalSink` over a `LocalDataMac` Keychain-sealed AES-256-GCM cipher. Simulator and automation runs create no database and no Keychain item.
- **Location and identity.** `~/Library/Application Support/<bundle id>/journal/` (directory owner-only, mode 0700), a Keychain service distinct from the TUI's (`dev.virtualrowing.pm5-diagnostic`), and never the repo's `Metrics/`. The app and TUI journals stay separate.
- **When the Keychain is touched.** Only on the Connect click, before the discovery is created, on the game thread, never at launch and never mid-row. A Keychain prompt may block on a system dialog, and that is acceptable at a user-initiated moment. A prompt or denial mid-row must be impossible by construction.
- **Journal-unavailable policy (needs owner confirmation).** If the journal or key cannot be opened (Keychain denied, disk error, ad-hoc rebuild changed access), the HUD shows a persistent "NOT BEING SAVED" state and the row starts only after an explicit "Row without saving" confirmation. The app never silently rows unjournaled, and never deletes or replaces an existing journal automatically.
- **Recovery at launch.** If the journal file already exists, run `ScanAndRecover` (it takes only the path, no key, so no Keychain access at launch) and show a one-line notice such as "1 interrupted session recovered". An interrupted session is marked and never resumed; a new row starts a new session. Recovery never creates the database.
- **Durability cadence.** The real-device path sets `FlushIntervalMs = 1000` (and a sample count that keeps chunks under one second at PM5 rates) so a crash loses at most the uncommitted second, per QA-003. Journal write latency is included in the evidence.
- **Quit.** Deinitialize ends an active session with existing runtime semantics, flushes, then disconnects. A crash or kill is the recovery path, not the quit path.

**Info.plist**
- Change `Build/Mac/Resources/Info.Template.plist` to: "Virtual Rowing uses Bluetooth to find and connect to your Concept2 PM5 rowing monitor and read your rowing data. It only scans after you choose to connect." Update `BLUETOOTH_USAGE_DESCRIPTION` in `Scripts/vir_dev/packaging.py` and the fixture in `Scripts/test_unreal_packaging.py` in the same commit; otherwise `make unreal-package-verify` fails. `Build/PM5Diagnostic-Info.plist.in` (the TUI's) is unchanged.

**Latency instrumentation (separable last commit; owner may cut to the gate run)**
- Measures QA-002's software portion: **T0** is the adapter's event `MonotonicTimestampNs` (stamped when the parsed event is queued), **T1** is when the HUD applies the new display generation on the game thread. Aggregated to count/p50/p95/max per run into an owner-only aggregate file under the app's Application Support directory; no raw payloads, identifiers or serial numbers. It is a lower bound on notification-to-visible (it excludes BLE receive-to-parse and render/present) and is reported as preliminary, never as a QA-002 pass. Physical stroke-to-display latency is out of scope.
- The spike verifies that the adapter and the subsystem share a timebase in the packaged process.

## Commit sequence and gates

1. **Link spike (gating).** Restructure CMake so `Concept2PMCore`, `Concept2PMMac` (with the generated profile header), `LocalDataMac` and the Swift shim build in the `VIR_APP_LIBS_ONLY` tree and join `VIR_APP_LIB_ROOTS`; extend `VirtualRowing.Build.cs` with the CoreBluetooth/Security/Swift link inputs. Pass = `make native-app` and `make unreal-smoke` succeed, and a Shipping link plus `otool -L` shows no Homebrew or non-system Swift load commands. If the Swift shim cannot link into a UBT module, stop and re-scope, as Milestone 5 did.
2. **Journal in the app.** Subsystem journal owner, Application Support location, launch recovery, failure policy, 1 s cadence, simulator-off guarantee. Testable with a temp directory and a test cipher.
3. **Real device path.** Discovery injection seam, connect/scan/reconnect/forget, ownership order, `Ready`-before-`Restored` order, disconnect/reconnect, denied/off/unsupported.
4. **Device panel widget** over the subsystem's device state.
5. **Info.plist, packaging verify and `CLAUDE.md`** updates.
6. **Latency instrumentation** (cuttable).
7. **Docs and owner runbook** (`make unreal-shipping`, launch, what to record, where the files land).

Each commit passes `make build`, `make test`, `make format-check`; commits 1, 2 and 5 also pass `make doctor` and `make unreal-smoke`.

## Automated tests

- An Automation spec drives the subsystem with a scripted fake `IConcept2PMDiscovery`/machine (through the injection seam): candidate ordering and labels, no discovery created before Connect, implicit remembered-PM5 reconnect then fallback to scan, the real adapter's `Ready`-before-`Restored` order, link loss and reconnect within and beyond the window, Bluetooth off/denied/unsupported states, ownership and teardown order, one poller, and the journal-on-for-real/off-for-simulator policy.
- Journal tests with a temp directory: recovery reports an interrupted session and creates nothing when no database exists; the journal-unavailable path requires explicit confirmation and never writes; cadence commits within 1 s; an abandoned (never-Deinitialized) subsystem's journal recovers in a new instance. This approximates process kill; a true kill remains an owner-run check.
- Unit tests: picker ordering/labelling with missing dBm and ties, and the latency aggregator's percentiles.
- CTest and `make unreal-package-verify`: plist string, self-containment of the new link inputs, `check_public_header_dependencies.py` (no Apple or Unreal types above the adapter).
- What no automated check covers: the real CoreBluetooth adapter inside the packaged app, the permission prompt, Keychain behavior in the ad-hoc bundle, and real firmware.

## Owner-run hardware evidence (per repo policy `make unreal-shipping` and app launches are owner-run)

Pass criteria come from the architecture's QA requirements, and results are recorded as a redacted note with no raw payloads, PM serial numbers or peripheral identifiers:

1. Fresh launch shows no Bluetooth prompt; Connect prompts once; the picker lists the PM5; connect; HUD metrics agree with the PM5 monitor within display resolution (FR-002, FR-003).
2. 6-minute row: no unexplained loss or duplication of distance against the monitor; latency summary recorded; journal shows a checkpoint within 1 s of activity (FR-007, QA-003).
3. Link loss (power-cycle or move the PM5 out of range) then return: HUD goes stale with no fabricated meters; the gap is recorded; in-range transient reconnect within 5 s p95 (QA-004; one run gives an observation, not a p95).
4. Process kill mid-row, then relaunch: the interrupted session is reported and the journal is intact.
5. Relaunch with a remembered PM5: implicit reconnect works, "Scan for a different PM5" and "Forget PM5" work.
6. Simulator launch (`-SimulatorDevice`) creates no database and no Keychain item.
7. Observed behavior when the row ends on the PM5 (`Complete`/`Terminated`), recorded as evidence for Milestone 4's simulator-only assumption.
8. Keychain behavior after re-running `make unreal-shipping` (rebuilt ad-hoc bundle): recorded whether it prompts or denies, and that the journal-unavailable path handles it.

## Out of scope

- Route, boat distance-to-spline, stroke animation, and FR-004 (a later milestone).
- Managed-workout (CSAFE) control; Just Row only (ADR-0010 keeps those runs in Phase 4).
- The Bluetooth TCC scenario matrix and signed/notarized builds (ADR-0008, ADR-0009). Behavior on the unsigned build is recorded, not certified.
- Cloud upload, the control API and any sync (build-order step 6).
- The 60-minute run, physical stroke-to-display latency, and a multi-PM5 disambiguation beyond nearest-first labelling.
- Any change to an accepted ADR or normative architecture text.

## Risks

- **Swift/CryptoKit shim inside a UBT-linked archive** is the least-proven piece (Swift autolink entries, runtime load commands, ARC settings on the Mac module). Mitigation: the gating spike, before anything else is built on it.
- **Keychain on an unsigned ad-hoc app.** The key is a legacy generic-password item; a rebuilt ad-hoc bundle has a different code identity, so access may prompt or be denied and strand a sealed journal. Never verified in a bundle yet. Mitigation: the prerequisite run and hardware item 8, the explicit journal-unavailable policy, and no automatic deletion.
- **The permission prompt is triggered by object construction.** Any code path that constructs the discovery early (a test, a probe, a preview) re-introduces launch prompts. Mitigation: a spec asserting no construction before Connect.
- **Discovery/machine lifetime.** A machine outliving its discovery is a use-after-free in the adapter. Mitigation: single ownership and teardown order, covered by the spec.
- **No in-session hardware verification.** A wiring bug can pass every automated check. Mitigation: keep the real path thin over already-tested components and keep the hardware checklist explicit; the milestone's `Status:` never claims hardware behavior until the owner records it.
- **Size.** This is the largest Phase 1 milestone so far. Mitigation: gated commit order; latency is cuttable; nothing later is blocked by cutting it.

## Open questions for the owner

Implemented on the proposed defaults; the owner may still veto any of them:

1. **Journal-unavailable policy:** the row is blocked until "Row without saving" is confirmed (as proposed).
2. **Latency instrumentation:** included (as proposed).
3. **Info.plist copy:** as written above.
4. Journal under Application Support with its own Keychain service; Connect always requires a click; the remembered PM5 reconnects implicitly after that click.

## Implementation notes (2026-09-19)

### What was built

- **Link (gating spike passed).** `CMakeLists.txt` now builds `Concept2PMCore`, `Concept2PMMac`, `LocalDataMac` and the Swift shim in the `VIR_APP_LIBS_ONLY` tree and adds the last two to `VIR_APP_LIB_ROOTS`; `VirtualRowing.Build.cs` adds the adapter/cipher include paths, the `Security`/`CoreFoundation` frameworks and the SDK `/usr/lib/swift` search path. The Editor module links (`make unreal-smoke`) and its load commands are system-only: `otool -L` shows only `/usr/lib/swift/libswift*.dylib`, no Homebrew and no `@rpath` Swift. `swiftc` ignores `CMAKE_OSX_DEPLOYMENT_TARGET` and stamped the host OS (26.0) into the shim; the shim now gets an explicit `-target` when a deployment target is set. `CMAKE_OBJCXX_VISIBILITY_PRESET` is hidden in the app tree like the C++ preset.
- **Engine-independent, CTest-covered (`Source/WorkoutRuntime`).** `FDeviceConnector` (the click-to-connect flow: lazy discovery, adapter-driven remembered-PM5 reconnect with a 3 s grace before scanning, nearest-first "PM5 #N (-dBm)" candidates, select/forget/stop, machine-before-discovery teardown), `FAppJournal` (owner-only directory, key through a cipher factory, no-key launch recovery that never creates a database), `FLatencyStats` and `WriteOwnerOnlyFile`, `MakeAppSessionConfig` (one poller; 1 s flush for a real device), `FNoOpJournalSink` (moved from the subsystem), and `FRealDeviceController`, which ties them together: journal-first `Connect()`, the journal-unavailable decision, session over the attached machine, End/Start New, quit-ends-the-row, and the latency aggregate. New CTest targets: `device_connector_tests`, `app_journal_tests`, `real_device_controller_tests`. `RowingDevice` gained `IRememberingMachineDiscovery` (which `IConcept2PMDiscovery` now derives from) so the connector needs no Apple or Concept2 header.
- **Unreal.** `UWorkoutSubsystem` owns a real-device controller when no `-SimulatorDevice` was asked for (created at launch, but it starts nothing) and exposes the panel model and actions; `UWorkoutDevicePanelWidget` is a separate code-only widget layered over the HUD (Z 10; a compact bottom-right cluster in the attached state), so the owner-verified HUD's focus logic was not touched beyond one `NoteDisplayApplied` call. The Automation spec gained four tests: no discovery or key until Connect, attach and journal-on, the journal-unavailable decision, and the simulator never offering the real flow.
- **Packaging.** `Info.Template.plist` uses the new Bluetooth usage text; `BLUETOOTH_USAGE_DESCRIPTION` and `verify_package` were updated with it, and `verify_package` now also rejects a non-system Swift runtime load command (`test_unreal_packaging.py` covers both).

### Findings and deviations from the plan

- **Recovery flag never cleared (fixed).** `ScanAndRecover().RecoveredAfterUncleanExit` stays true on every later scan by design (its test asserts that), so a "recovered a session" notice built on it would show on every launch forever. `FLocalDataRecoveryReport` gained an additive `NewlyRecoveredSessionCount` (non-zero only on the scan that records the marker), the app notice uses it, and the same one-line fix was applied to the `pm5-tui` journal driver's banner, which had the same bug. `LocalDataJournalTests` covers first-scan 1, second-scan 0.
- **The Editor must never reach Bluetooth (added).** The Editor's bundle has no Bluetooth usage description, so creating a `CBCentralManager` there would terminate it, after the Keychain had already been touched. `FRealDeviceDependencies::bTransportAvailable` (false in `WITH_EDITOR` builds) makes `Connect()` stop with `EDeviceProblem::TransportUnavailable` before the journal or the discovery is touched; the panel says so and offers `-SimulatorDevice`. Not in the reviewed plan.
- **No dedicated unsupported-firmware panel state.** The planned state is not a separate mode: an attached machine the adapter rejects surfaces through the HUD's connection label and banner (`Failed`), with the panel's Change PM5 available. A dedicated message is follow-on work.
- **Connector polls for the remembered machine only while `Starting`**, not while scanning, to avoid a queue hop per frame; a remembered machine that appears after a scan began is not adopted.
- **Controller in `WorkoutRuntime`, not in the Unreal module** as planned, so the whole flow is CTest-tested with fakes and the Automation spec stays thin. The planned "discovery injection seam" is `FDeviceConnector::FDiscoveryFactory`.
- **Journal directory is `~/Library/Application Support/dev.virtualrowing.app/journal`, metrics `.../metrics`; Keychain service `dev.virtualrowing.app`.** Latency is written on session end and quit as `hud-latency-<unix ms>.json` (aggregate p50/p95/p99/max only).
- **Latency is measured at the subsystem/HUD boundary:** T0 is the adapter event timestamp of the oldest unapplied sample, T1 is when the HUD applies the display generation. It is a lower bound and never a QA-002 pass.
- **Not committed.** The commit sequence above was followed as a build order; the changes are in the working tree because no commit was requested.

### Verified and not verified

Verified in-session: `make test` (26/26, including the three new targets and the extended LocalData recovery test); `make format-check`; `make doctor`; `make native-app`; `make unreal-smoke` (Editor Development target compiles and links the adapter and CryptoKit); `python3 Scripts/test_unreal_packaging.py`; the headless Editor Automation run of `VirtualRowing.WorkoutSubsystem` (11/11, seven existing and four new), after which no `dev.virtualrowing.app` directory or Keychain item existed.

Not verified during the implementation session: the Bluetooth permission prompt and the Keychain in the packaged ad-hoc app; the packaged Shipping link and `make unreal-package-verify`; the device panel's appearance and focus in a running app (the widget is compiled, not exercised: the Automation run is headless with no viewport); the prerequisite `make pm5-tui-journal` run.

Owner-run evidence update (2026-09-20): `make unreal-shipping` and `make unreal-package-verify` **passed** at revision `c8d78d720daf` on arm64 macOS 26.6.2 with Xcode 26.1.1 and Unreal Engine 5.8.2. This closes the Phase 1 packaged unsigned-build evidence row; the packaged-app interaction and hardware checks remain separate evidence.

Owner-run evidence update (2026-09-20): the owner ran the app with a real PM5 and observed the HUD shown. This records only HUD visibility; PM5-monitor metric agreement, journaling, durability, packaged-app, and other hardware-checklist evidence remain outstanding.

Owner-run evidence update (2026-09-20): real-PM5 discovery, pairing, and reconnection **passed**. This satisfies the recorded FR-002 owner-run outcome; the remaining checklist items above remain independent evidence.

## Owner action required

1. **Prerequisite:** run `make pm5-tui-journal` on a real PM5 (closes the outstanding Milestone 2 and 4 confirmations).
2. The 2026-09-20 owner run completed `make unreal-shipping` and `make unreal-package-verify` (checking the new plist text and the Swift and Homebrew load commands on the real Shipping binary). Launch the staged app and run the remaining checks in "Owner-run hardware evidence". Records go into a redacted note; the journal, the latency aggregate and any captures are private athlete data and are never committed.
3. Expected on first Connect: one Bluetooth prompt and one Keychain prompt (the latter may repeat after each rebuild of the ad-hoc bundle; see Risks).
