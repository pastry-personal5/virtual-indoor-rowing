# Repository instructions

## Start here

This is an architecture-first repository; no executable product build is checked in yet. Before editing, read [README.md](README.md), [ARCHITECTURE.md](ARCHITECTURE.md), the affected detailed specification in `docs/architecture/`, and relevant ADRs in `docs/adr/`.

- Architecture text using **must**, **shall**, or an accepted ADR is normative.
- Research under `docs/archive/research/` is dated evidence, not requirements.
- Accepted ADRs change only through a superseding ADR.
- Milestone delivery history belongs in `docs/m<N>/CHANGELOG.md`; it does not change architecture.

## Architecture boundaries

- Target: Apple-silicon macOS Tahoe 26.6.2+, Unreal Engine 5.8 at an approved patch, and initially a Concept2 Model D with PM5 over BLE.
- Keep domain logic engine-independent and hardware-neutral. Concept2 and Apple APIs stay in Objective-C++ adapters below the rowing-machine interface.
- Preserve four distinct truths: PM5 measurements, the local durable session, the server-authoritative ranked result, and reversible client presentation.
- Unreal UI, world, and rendering consume domain snapshots. They do not decode device data, persist official sessions, or determine race results.
- Offline local workout completion is required. Network failures cannot discard it or fabricate measurement data.
- Version externally visible protocols and persisted schemas before implementation. Keep public contracts small and backward compatible for the supported client window.

## Repository layout

| Path | Purpose |
|---|---|
| `Source/` | Unreal and engine-independent C++ modules |
| `Plugins/Concept2PM/` | CoreBluetooth and PM protocol adapter |
| `Contracts/proto/` | Versioned client/cloud contracts |
| `Services/` | Go control plane, race service, and workers |
| `Tools/` | Simulator, replay, and content tools |
| `Infra/terraform/` | Cloud infrastructure |
| `Tests/` | Fixtures and automated tests |
| `Content/` | Unreal assets; large assets use Git LFS |
| `docs/` | Specifications, ADRs, milestones, and research |

These code roots are planned. Do not create unrelated structure before the relevant milestone authorizes it.

## Implementation and tests

- Follow Epic C++ conventions: tabs, braces on new lines, and `U`, `A`, `F`, `E`, `I`, and `T` prefixes. Format Go with `gofmt`.
- Use `lower_snake_case` Protobuf fields and never reuse field numbers.
- Add tests and traceable evidence for every `FR-*` or `QA-*` change. Use unit/property tests, PM5 simulator fixtures, deterministic race replays, and hardware-in-loop testing as applicable.
- Use behavioral test names, such as `pm5_reconnect_same_device` and `TestFinalizeSessionIdempotent`.
- A simulator does not replace required release hardware evidence.
- Expose reproducible build and test commands through checked-in scripts or a `Makefile` when code arrives. Unreal packaging uses `RunUAT.sh`/BuildGraph.

For documentation-only changes, run:

```sh
rg --files README.md docs
git diff --check
git status --short
```

## Changes, reviews, and security

- Keep changes scoped; preserve unrelated working-tree edits.
- Use imperative, scoped commit subjects, for example `docs: clarify PM5 authority` or `feat(device): decode general status`.
- Pull requests state intent, affected requirements/ADRs, verification, and risks. Include UI screenshots, schema rollback notes, and performance or PM5 evidence when applicable.
- Never commit secrets, signing identities, PM serial numbers, production captures, provider credentials, or generated Unreal directories: `Binaries/`, `DerivedDataCache/`, `Intermediate/`, and `Saved/`.
- Pin toolchains and dependencies; redact diagnostics. Preserve offline workout durability and server-authoritative race boundaries.
