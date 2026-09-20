# Delivery Phase 1: walking skeleton

Status: Complete — Pass (2026-09-20)
Owner: A0 — CTO / Principal Architect
Last reviewed: 2026-09-20

## Outcome

Phase 1 delivered an unsigned, ad-hoc internal macOS rowing instrument: real
PM5 and simulator input, live HUD, gray-box route presentation, durable local
sessions and recovery, plus a development-only session-sync slice. It is not a
production release. Executable signing, notarization, and the update channel
remain Phase 4 work under [ADR-0008](../adr/0008-defer-macos-signing-to-phase-4.md).

The combined exit gate passed on the evidence in
[the Phase 1 report](04-evidence-report.md). Development and single-user
journals are owner-only plaintext under [ADR-0012](../adr/0012-plaintext-development-single-user-journals.md);
the encrypted per-profile design remains required before multi-user or release
scope.

## Milestone record

| Milestones | Delivered boundary |
|---|---|
| [1](01-milestone-1-rowingcore-domain-contract.md)–[3](03-milestone-3-local-data-session-journal.md) | Domain contracts, PM5 capabilities, and local journal |
| [4](04-milestone-4-workout-runtime.md)–[6](06-milestone-6-hardening-and-refactor.md) | Workout runtime, simulator HUD, and hardening |
| [7](07-milestone-7-real-pm5-app-wiring.md)–[8](08-milestone-8-gray-box-course-presentation.md) | Real-PM5 app path and gray-box course presentation |
| [Milestone 9](09-milestone-9-development-session-sync.md)–[Milestone 11](11-milestone-11-closeout-and-sync-hardening-chores.md) | Development sync and closeout; includes Milestone 10 persistent sync |

See the [changelog](CHANGELOG.md) for delivery history and the
[delivery plan](../architecture/10-delivery-plan.md) for phase requirements.
Milestone completion is not itself a phase exit decision.
