# AGENTS.md

## Read first

This is an architecture-first repository. Before changing code or docs, read
`README.md`, `ARCHITECTURE.md`, the affected `docs/architecture/` specification,
and relevant accepted ADRs. Normative text uses **must**/**shall**; accepted
ADRs change only through a superseding ADR. Research in `docs/archive/research/`
is evidence, not requirements.

Current state: Phase 0 and Phase 1 are complete; Phase 2 is planned and has
not started. Phase status and exit gates live in `docs/architecture/10-delivery-plan.md`
and the corresponding phase packet. Milestones never pass a phase gate alone.

## Architecture rules

- PM5 facts are authoritative; never synthesize distance after a gap or reconnect.
- The local append-only SQLite journal preserves workout continuity; cloud or
  rendering failures cannot discard a completed row.
- The authoritative race server, never client presentation, determines rank.
- Keep platform/framework adapters flowing toward engine-independent contracts.
  `RowingCore` must build and test without Unreal, CoreBluetooth, network, or
  a database runtime.
- Apple/Concept2 types stop in `Plugins/Concept2PM`; Unreal UI/world code
  consumes snapshots only and stays on the game thread.
- Development/single-user journals are owner-only plaintext under ADR-0012.
  Do not weaken privacy handling or commit journals, raw captures, serials,
  credentials, or generated Unreal output.

## Commands

Run commands from the repository root through `make`/`Scripts/dev.py`; do not
invoke CMake, CTest, or clang-format directly.

```sh
make doctor             # verify pinned host toolchain
make build && make test # native build and test suite
make format-check       # formatting check
make unreal-native-app  # native archive consumed by Unreal
make unreal-smoke       # Unreal Editor compile
make unreal-shipping    # unsigned arm64 Shipping package
make han-external-cook  # external Han HanRiver.{pak,utoc,ucas} cook
make unreal-package-verify
make phase1-check       # Phase 1 packet validation
make development-sync-test
```

`make pm5-tui-hil` and `make pm5-tui-journal` create private local evidence;
never commit or attach their output. `make release-sign-notarize` is a
credential-owner procedure; signing/notarization is Phase 4 work.

## Unreal MCP

When a task needs live Unreal Editor state or editor-owned changes, first check
whether the configured Unreal MCP server is alive by listing its tools and
making a read-only editor query. If that succeeds, use Unreal MCP for the
needed editor inspection, asset/map/Blueprint work, automation, or log access.
Inspect before mutating, keep changes bounded to the task, and follow any
phase-specific review gate; in particular, the Phase 2 representative-scene
review still gates Han River asset mutation.

Do not use Unreal MCP for engine-independent source, documentation, native
builds/tests, packaging, or other work that does not need a live editor. An
open port alone is not a health check. If an editor-dependent task needs Unreal
MCP and the server is unavailable, report that work as blocked or unverified
instead of silently replacing editor operations with direct `.uasset` edits.
Unreal MCP does not replace the required `make`/`Scripts/dev.py` verification.

## Change and handoff rules

- Follow Epic C++ conventions: tabs, braces on new lines, and Unreal type
  prefixes. `.clang-format` has no line-length limit.
- Add traceable tests/evidence for every changed `FR-*` or `QA-*` behavior.
  Simulator evidence does not replace required hardware evidence.
- Do not create new top-level structure before the owning phase authorizes it.
- Record milestone history in the owning phase `CHANGELOG.md`; a changelog
  cannot change architecture or an ADR.
- For documentation-only changes, run `rg --files README.md docs`,
  `git diff --check`, and `git status --short`. For all changes, report exactly
  what was verified and what remains unverified.
