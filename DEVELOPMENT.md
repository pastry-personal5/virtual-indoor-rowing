# Development guide

## Current state

The repository has a native diagnostic build and test system, plus an empty Unreal smoke host. It is not yet a complete rowing product. The active bounded work is the [diagnostic foundation packet](docs/phase-0/README.md), which contributes evidence to delivery Phase 0; its real-hardware and Unreal smoke gates remain separate from native test success.

## Supported baseline

- Apple-silicon Mac running macOS Tahoe 26.6.2 or later.
- Xcode 26.1.1 and an approved Unreal Engine 5.8 patch.
- Concept2 Model D with PM5 over BLE for hardware validation.

These are pinned architecture decisions, not suggestions. A replacement needs compatibility evidence and, where the decision changes, an ADR.

## Setup and verification workflow

1. Read [ARCHITECTURE.md](ARCHITECTURE.md), [CONTRIBUTING.md](CONTRIBUTING.md), and the relevant delivery phase, milestone, and specification.
2. Set `UE_ROOT` to the approved stock Unreal 5.8.2 installation if it is not in a recognized default location.
3. Inspect the working tree before editing; preserve unrelated changes.
4. Run the pinned native checks:

   ```sh
   make doctor
   make configure
   make build
   make test
   make format-check
   ```

5. Run `make unreal-smoke` only after `make doctor` passes. Run `make pm5-tui-hil` for a user-driven hardware session; simulator success is not hardware evidence. Invoking `make pm5-tui-hil` explicitly enables the bounded raw-telemetry hardware probe described below.
6. Before handoff, run `git diff --check` and `git status --short`, then state exactly what was verified and what remains unverified.

Generated native build output is under `Build/native/`. The checked-in `make clean` command removes only that directory.

## Unreal MCP workflow

Use the configured Unreal MCP server when work requires a live Unreal Editor:
editor-state inspection, asset/map/Blueprint operations, content-browser or
viewport interaction, Unreal automation, and editor log inspection. Before
depending on it, verify end-to-end health through the MCP client by listing
the server tools and completing a read-only editor query. A listening HTTP
port by itself is insufficient.

Start with inspection and limit mutations to the authorized task. Repository
and phase rules still apply; for example, the
[Han River course-content plan](docs/phase-2/03-han-river-course-content-plan.md)
does not permit Unreal-MCP asset mutation until its representative-scene
review authorizes production. Save intended source-controlled editor changes,
then run the applicable `make`/`Scripts/dev.py` checks; MCP success is not
build, cook, package, performance, or hardware evidence.

Do not route documentation, engine-independent source, native builds/tests,
packaging, or other non-editor work through Unreal MCP. If required editor work
cannot use the configured server, record it as blocked or unverified rather
than editing binary Unreal assets outside the editor.

### Editor crash prevention and build failures

An open Editor does not block ordinary source work. For Phase 2 Milestone 5,
run `make water-source-check` while authoring: it checks formatting, builds and
tests the engine-independent native targets, and validates the tracked Han
source dependencies. Keep editing material recipes, presentation source, tests,
and documentation while the Editor is open. Use Unreal MCP for editor-owned
asset changes when its read-only health query succeeds. Record Unreal compile,
automation, cook, packaged visual, and GPU gates separately; the source check
does not pass them.

For native Unreal changes, save work and close **all** Unreal Editors and
commandlets, run `make unreal-smoke`, then reopen the project normally and run
automation in that fresh process. Keep Editor closed through Shipping/Han builds
too. The wrappers check executable names before native work and again immediately
before UBT/UAT; `clean-unreal` also refuses to remove generated state while Editor
is open. If process inspection is denied, they stop with an unknown-state error.
They do not use the MCP port or stale `EditorRuns` files as proof of closure.

The smoke build targets `VirtualRowingEditor` with `-NoHotReload` and checks
`Binaries/Mac/UnrealEditor.modules` selects the freshly built base module. Its
ordinary UBT log is retained at `Saved/Logs/UnrealBuildTool.log`; relocating that
log does not relocate the engine's separate cache/trace writes. Both cook paths
also pass `-NoHotReload` and require the pinned `make doctor` checks.

`UE_ROOT` (or `unreal.installation_root`) must name an absolute, existing engine
installation. A bad explicit path fails instead of falling back to another engine.
Valid symlinks resolve to the canonical installation. On this host,
`/Users/user1/ue-work/work/UE_5.8` and
`/Volumes/Unreal_Engine_Volume/work/UE_5.8` resolve to the same directory, as do
the project alias `/Users/user1/work/cur` and the volume worktree. The observed
crash stacks do not establish these aliases as a crash cause. Leave old cache
files and installed-engine paths alone; do not repair them speculatively.

The 2026-09-25 investigation distinguished these failures using local crash
contexts, macOS reports, build logs, and the installed UE 5.8.2 source:

- **Material editing:** the September 24 fatal `!IsRooted()` stack runs through
  `DeleteAllMaterialExpressions` / `DeleteMaterialExpression`. The water recipes
  preserve existing expressions and reconnect outputs. Do not delete inspected
  expressions or forcibly remove engine-owned roots. Disconnected expressions
  remain in the asset; repeated recipe runs can grow its source graph.
- **Native automation after hot reload:** the September 24 `bAllRegistered`
  ensure names `HOTRELOAD_GrayBoxCourseActor` and a retained old automation dylib.
  The preceding `Module Recompile VirtualRowing` loaded module 6137, then logged
  that both `FCoursePresentationSpec` and `FWorkoutSubsystemSpec` were already
  registered and would not be replaced. The course retry executed module 0009.
  Save work, close the project Editor, compile with `make unreal-smoke`, and
  reopen before native automation. CoursePresentation rejects a replaced class
  before queuing test setup. Do not treat a hot-reloaded spec as fresh evidence.
- **Test-world initialization:** the September 20–21 `WorldSettings` name
  collisions came from initializing a world twice. `UWorld::CreateWorld`
  already initializes it; the existing spec passes initialization values into
  that call and does not call `InitializeNewWorld` again.
- **Cook MCP contention:** both cook commands pass a process-local
  `AdditionalCookerOptions` INI override disabling MCP auto-start. Interactive
  Editor settings remain unchanged. Do not temporarily edit shared MCP settings to cook.
- **Build-process abort:** UBT rotates its per-user `Trace.uba` before parsing
  `-NoLog` or `-NoUBA`. The smoke wrapper checks write access to
  `~/Library/Application Support/Epic/UnrealBuildTool` before starting UBT.
  Run the target from an owner terminal if this is denied. Do not supply a fake
  recursive `-Session=`: UE 5.8's executor still requires the standalone trace.
- **`XmlConfigCache` permission denial:** AutomationTool's
  `PlatformExports.Initialize` calls `XmlConfig.ReadConfigFiles(null, null)`
  before `BuildCookRun`, ignoring its project scope at this stage. Stock installed
  macOS engines therefore need write access to
  `~/Library/Application Support/Epic/UnrealEngine`, including its
  `Intermediate/Build` directory. Both cook wrappers check this before native
  build/provenance work. Run `make unreal-shipping` or `make han-external-cook`
  from an owner terminal with access if the managed runner denies it. This is a
  build failure before cooking, not evidence of an Editor crash or a corrupt cache.
  `-ubtargs=-XmlConfigCache=...` cannot redirect UAT initialization; UBT's explicit
  cache option only loads an already-generated cache and bypasses freshness checks.

The older missing-Metal-toolchain shader failures are covered by `make doctor`.
macOS `_RegisterApplication` launch aborts and CrashReportClient's own shutdown
crashes are separate host/engine failures; changing XML caches does not repair
them. Keep raw crash files local and record sanitized findings in the phase log.

## Implementation workflow

1. Keep reproducible commands in `Scripts/dev.py` and the root `Makefile`; do not rely on personal shell setup.
2. Build and test domain code without Unreal where possible. Keep CoreBluetooth and other Apple APIs in Objective-C++ adapter code.
3. Add deterministic fixtures and simulator/replay scenarios to `Source/RowingSim/`. Sanitize fixtures and never include athlete data or serial numbers.
4. Use UnrealBuildTool for Unreal modules. Package with `RunUAT.sh`/BuildGraph as specified in the delivery architecture.
5. Validate protocol/schema compatibility, journal recovery, and idempotency before dependent UI or cloud work.
6. Run the physical PM5 acceptance procedure in the diagnostic milestone plan; simulator or short diagnostic output alone is insufficient.

## Source line length

There is no maximum characters-per-line limit for source files. The checked-in `.clang-format` sets `ColumnLimit: 0`, which disables clang-format's column limit. Keep line breaks where they help readability, but do not wrap code solely to satisfy a character count. Other formatters or linters added later must also be configured without a maximum line length.

## PM5 TUI logs

The PoC TUI creates `Logs/pm5-tui/` on launch and writes `pm5-tui.log` there. The default threshold is `DEBUG`; files rotate at 1 MiB, retaining three numbered backups. The directory and log files are owner-only, and generated log files are ignored by Git. Diagnostic logging records commands, lifecycle events, and redacted faults, but excludes peripheral IDs, PM serials, raw BLE payloads, and metric values such as heart rate.

Every launch also creates one owner-only, Git-ignored JSONL file under
`Metrics/pm5-tui/`. Ordinary `make pm5-tui` runs record normalized and aggregate
evidence without raw payloads. The explicit `make pm5-tui-hil` hardware-probe mode
additionally records bounded rowing-service telemetry payloads, UUID short IDs,
parser outcomes, approved lengths, per-characteristic sequence numbers, and
monotonic receive timestamps. The TUI and log visibly state when this capture is
active. Capture stops after 65 minutes or 100,000 packets and reports any
truncation, queue overflow, or limit drop in the final record. It never captures
identity-characteristic payloads, peripheral identifiers, or PM serial numbers.
Treat hardware-probe JSONL as private athlete/device evidence: inspect it
locally, scrub any exported fixture, and do not commit or attach the raw file.

## Change-specific checklist

| Change | Minimum additional work |
|---|---|
| PM5 codec or connection lifecycle | Unit/property tests, simulator/replay scenario, redacted hardware evidence when required |
| Domain contract or Protobuf schema | Versioning and compatibility check; update all consumers |
| Journal or cloud persistence | Migration and crash/idempotency coverage; rollback notes |
| Unreal presentation | Automation/content validation and screenshot or performance evidence as relevant |
| Release/build tooling | Pinned version update, reproducible CI command, packaging/security verification |

## Useful references

- [Delivery Phase 0 Milestone 1 implementation plan](docs/phase-0/03-milestone-1-implementation-plan.md)
- [Verification strategy](docs/architecture/09-verification-strategy.md)
- [Delivery and operations](docs/architecture/08-delivery-and-operations.md)
- [Accepted ADRs](docs/adr/)
