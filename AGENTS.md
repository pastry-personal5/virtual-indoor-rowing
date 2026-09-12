# Repository Guidelines

## Project Structure & Module Organization

This is currently an architecture-first repository. Start at `README.md` and `docs/README.md`.

- `docs/architecture/`: normative product, client, backend, security, operations, testing, and delivery designs.
- `docs/adr/`: accepted architecture decisions. Reverse a decision with a superseding ADR.
- `docs/archive/research/`: dated evidence, not requirements.
- Planned code roots: `Source/`, `Plugins/Concept2PM/`, `Contracts/proto/`, `Services/`, `Tools/`, `Infra/terraform/`, and `Tests/`; Unreal assets belong in `Content/`.

Do not commit Unreal-generated `Binaries/`, `DerivedDataCache/`, `Intermediate/`, or `Saved/` directories. Store large Unreal assets with Git LFS.

The supported desktop target is **macOS Tahoe 26.6.2 or later** on Apple silicon; see ADR-0001.

## Build, Test, and Development Commands

No executable build system is checked in yet. Useful documentation-stage checks are:

```sh
rg --files README.md docs
git diff --check
git status --short
```

These list documents, detect whitespace errors, and show change scope. When code arrives, expose reproducible commands through checked-in scripts or a `Makefile`. Unreal packaging should use `RunUAT.sh`/BuildGraph as specified in `docs/architecture/08-delivery-and-operations.md`.

## Coding Style & Naming Conventions

Follow Epic’s Unreal C++ conventions: tabs, braces on new lines, and standard `U`, `A`, `F`, `E`, `I`, and `T` prefixes. Keep domain logic engine-independent and Apple APIs in Objective-C++ adapters. Format Go with `gofmt`. Use `lower_snake_case` Protobuf fields and never reuse field numbers. Name ADRs `NNNN-short-decision.md`.

## Testing Guidelines

Every `FR-*` and `QA-*` change needs traceable evidence. Add unit/property tests for codecs and rules, PM5 simulator fixtures, deterministic race replays, and hardware-in-loop coverage. Use behavioral names such as `pm5_reconnect_same_device` or `TestFinalizeSessionIdempotent`. Simulators do not replace release hardware tests.

## Commit & Pull Request Guidelines

There is no established history yet. Use imperative, scoped subjects such as `docs: clarify PM5 authority` or `feat(device): decode general status`. Pull requests must state intent, affected requirements/ADRs, verification, and risks. Include screenshots for UI changes, schema rollback notes, and performance or PM5 evidence when relevant.

## Security & Configuration

Never commit tokens, signing identities, PM serial numbers, production captures, or provider secrets. Pin toolchains and dependencies, redact diagnostics, and preserve offline workout durability and server-authoritative race boundaries.
