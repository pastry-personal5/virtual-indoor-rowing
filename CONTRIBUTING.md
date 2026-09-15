# Contributing

## Before you change anything

Read [ARCHITECTURE.md](ARCHITECTURE.md), [AGENTS.md](AGENTS.md), the affected detailed specification, and relevant ADRs. Architecture documents are normative where they say **must**, **shall**, or cite an accepted ADR. Research notes are evidence, not requirements.

Keep changes small and scoped. Do not couple gameplay, UI, or cloud code to Concept2 types; do not let Unreal presentation decide workout facts or race results.

## Repository map

| Path | Owner / content |
|---|---|
| `Source/` | Unreal game and engine-independent modules |
| `Plugins/Concept2PM/` | PM5/CoreBluetooth adapter |
| `Contracts/proto/` | Versioned cross-service contracts |
| `Services/` | Go control plane, race service, and workers |
| `Tools/` | Simulator, replay, and content tools |
| `Infra/terraform/` | Cloud infrastructure |
| `Tests/` | Fixtures and automated coverage |
| `Content/` | Unreal assets; use Git LFS for large binaries |
| `docs/` | Specifications, ADRs, phase-scoped milestones, and research |

The code roots are planned; do not create unrelated structure before a milestone in the relevant delivery phase authorizes it.

## Engineering rules

- Follow Epic C++ conventions: tabs, braces on new lines, and standard `U`, `A`, `F`, `E`, `I`, and `T` prefixes.
- Keep domain logic engine-independent. Put Apple APIs in Objective-C++ adapters. Format Go with `gofmt`.
- Use `lower_snake_case` Protobuf fields; never reuse a field number.
- Version every externally visible protocol and persisted schema before implementation.
- Preserve actual PM5 measurement truth separately from virtual/game-balanced presentation state.
- Never commit secrets, serial numbers, signing identities, production captures, or generated Unreal directories (`Binaries/`, `DerivedDataCache/`, `Intermediate/`, `Saved/`).

## Tests and evidence

Every `FR-*` or `QA-*` change needs traceable evidence. Add the appropriate unit/property, contract, simulator/replay, integration, Unreal, or hardware-in-loop coverage. Simulators do not replace release hardware evidence. Use behavioral test names such as `pm5_reconnect_same_device` and `TestFinalizeSessionIdempotent`.

During the documentation-only stage, run:

```sh
rg --files README.md docs
git diff --check
git status --short
```

## Pull requests and documentation

Use imperative, scoped commit subjects, for example `docs: clarify PM5 authority` or `feat(device): decode general status`. A pull request states intent, affected requirements/ADRs, verification, and risks. Include UI screenshots, schema rollback notes, and performance or PM5 evidence when applicable.

Use an ADR to reverse an accepted decision. Every delivery phase has one or more milestones and may have many; each milestone belongs to exactly one phase and uses the name `Phase <N> Milestone <M>`. Record material milestone history in `docs/phase-<N>/CHANGELOG.md`, identifying the milestone in every entry. Milestone completion does not pass the phase exit gate. Do not put generated artifacts, secrets, or routine formatting edits there.
