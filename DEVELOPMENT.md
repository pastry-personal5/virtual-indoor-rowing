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
