# ADR-0009: Defer the Bluetooth TCC permission-scenario matrix until Phase 4

- Status: Accepted
- Date: 2026-09-17
- Owners: CTO, release lead, verification lead

## Context

[Phase 0 Milestone 3](../phase-0/06-milestone-3-toolchain-shipping.md)'s acceptance gate named a human-only test matrix as item 3: three TCC-reset Bluetooth probe scenarios (fresh Allow, Deny, Settings-repair-then-Allow) and a normal-launch-no-prompt check, each requiring a real macOS TCC database reset and an observed permission dialog. [The delivery plan](../architecture/10-delivery-plan.md) independently named the same evidence, worded as "fresh install/permission denial/repair pass," as a Phase 1 exit-gate item, and [system architecture](../architecture/02-system-architecture.md) stated the packaged application "passes the Bluetooth permission test from Phase 0."

The unsigned toolchain path itself (`make unreal-shipping`, `make unreal-package-verify`, `make toolchain-bluetooth-probe`) is proven: item 1 (native/Unreal build, format, package verification) and item 2 (self-hosted CI on `main`) are both satisfied with redacted evidence in [the evidence report](../phase-0/04-evidence-report.md). The probe already returns `authorized_powered_on` and `denied` result states from ad-hoc manual runs, showing the CoreBluetooth authorization/scan/schema-versioned-result path works. What remains is not proof the mechanism works, but a supervised, repeatable TCC-reset regression matrix — the same category of small-cohort, human-driven verification this project has already deferred once for signing/notarization in [ADR-0008](0008-defer-macos-signing-to-phase-4.md), and for the same reason: standing up a repeatable manual reset/approve/deny rehearsal now serves no cohort yet, since Phase 0/1/2/3 testers remain the internal team and small named partners who can be walked through any permission state directly.

## Decision

- Phase 0 Milestone 3's acceptance-gate item 3 (fresh Allow, Deny, Settings-repair-then-Allow, normal-launch-no-prompt) is no longer a condition of Milestone 3 or the delivery Phase 0 exit gate. Milestone 3 is complete on items 1 and 2 alone.
- The delivery plan's Phase 1 exit gate no longer names "fresh install/permission denial/repair pass" as a requirement; that scenario matrix moves to the Phase 4 exit gate.
- System architecture's release-acceptance checklist is corrected: the Bluetooth-permission scenario matrix is a Phase 4 check, not a Phase 0 one. Phase 0 only requires the basic authorized/scan/schema-result path the probe already demonstrates.
- Phase 4 introduces the full matrix — fresh Allow after TCC reset, Deny, Settings-repair-then-Allow, and normal-launch-no-prompt — run against the then-current signed/notarized build alongside the ADR-0008 signing/notarization rehearsal, so both human-only regression matrices are exercised together once a real external cohort exists.
- Until Phase 4, the evidence report records these four rows as Deferred to Phase 4, not Pending.

## Consequences

- Phase 0 Milestone 3 and the delivery Phase 0 exit gate no longer block on human-only TCC rehearsal; Milestone 3 closes on reproducible, already-collected evidence.
- Phase 1's exit gate is lighter by one manual-only item; the underlying risk (a tester denies Bluetooth and cannot recover, or the app prompts unexpectedly) is unmitigated by a repeatable gate through Phases 1–3, same as the signing risk ADR-0008 already accepted for that window. Internal/named-partner cohorts can be walked through any TCC state directly if it occurs.
- Phase 4 gains a second human-only regression matrix (permission scenarios) alongside the existing ADR-0008 signing/notarization rehearsal; its exit gate and timeline must account for both.
- If a Phase 0–3 cohort needs to grow beyond a small named group before Phase 4, or if testers report unrecoverable permission states, this deferral must be revisited and the matrix pulled forward.

## Alternatives considered

- **Keep item 3 in Milestone 3 as originally scoped:** rejected for now; it blocks Milestone 3 completion on a manual rehearsal that a small internal/named cohort does not yet need, while the mechanism it would validate is already evidenced by ad-hoc probe runs.
- **Keep the requirement at Phase 1 instead of moving it to Phase 4:** considered, since Phase 1 already named it; rejected to keep the deferral aligned with ADR-0008's existing signing/notarization timeline and cohort size rather than creating a second, earlier gate for a related human-only rehearsal that the same cohort argument applies to.
- **Drop the matrix requirement entirely:** rejected; a real permission-denial/repair regression matters before any external or public exposure, so it stays a hard Phase 4 exit-gate item, not an optional nice-to-have.

## Validation and revisit

Revisit if a Phase 0–3 cohort needs to grow beyond a small named group before Phase 4, if a tester reports an unrecoverable Bluetooth-permission state, or if Apple changes TCC reset/prompt behavior in a way that invalidates the deferred matrix's assumptions.
