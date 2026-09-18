# ADR-0011: Defer the design-partner cohort from Phase 1's exit gate to Phase 4

- Status: Accepted
- Date: 2026-09-18
- Owners: CTO / Principal Architect (A0)
- Supersedes: the design-partner cohort disposition in [ADR-0010](0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md) (that ADR's other decisions — Milestone 4 Spike A/B/C, the realtime spike, and opening Phase 1 — are unaffected and remain in force)

## Context

[ADR-0010](0010-defer-remaining-phase-0-spike-evidence-to-phase-4.md) moved the design-partner cohort item off the Phase 0 exit gate and onto Phase 1's, reasoning that Phase 1's own exit gate in [the delivery plan](../architecture/10-delivery-plan.md#phase-1--walking-skeleton-46-weeks) already independently required "design partners can attempt the supported setup without an engineer driving the UI." That ADR explicitly rejected deferring the cohort item to Phase 4 alongside the other Milestone 4/realtime items, on the grounds that Phase 1's exit gate already needed a cohort to exist, so deferring it three phases would create an internal contradiction.

That premise no longer holds: recruiting named design partners on the current timeline is not feasible, and keeping it as a hard Phase 1 exit-gate condition would block Phase 1 from closing on a constraint outside engineering's control, independent of whether the walking-skeleton deliverable itself (FR-002/003/004/007, the 6-minute hardware run, no-account row completion) is otherwise ready.

## Decision

- The design-partner cohort item is removed from delivery Phase 1's exit gate. Phase 1 closes on its remaining criteria (FR-002/003/004/007 demonstrated on real Model D/PM5 and simulator; the 6-minute hardware run; no account/subscription/external integration/multiplayer required to complete the row) without requiring named design partners to have attempted the setup.
- The cohort requirement is deferred to delivery Phase 4's exit gate, alongside the other items ADR-0010 already deferred there (Spike A step 4 real-PM5 managed-workout runs, the full visual-performance evidence, the realtime spike, ADR-0008 signing/notarization, and the ADR-0009 TCC matrix). Phase 4 is revised to require: named design partners (or a successor cohort, if Phase 2/3's own external-cohort work has since superseded the need for a separately named Phase 1-era group) have attempted the supported setup without an engineer driving the UI, with failures categorized.
- Recruiting design partners remains legitimate work at any point from Phase 0 onward (per [ADR-0007](0007-evidence-gated-product-delivery.md)'s "treat customer research, design-partner recruitment... as delivery work with named owners and artifacts" and the delivery plan's Phase 0 business/external actions) — this ADR only removes it as a hard *gate*, not as ongoing product work. If a cohort is recruited and observed before Phase 4, that evidence still counts toward Phase 4's gate; the deferral is a ceiling, not a restart.

## Consequences

- Phase 1 can close purely on engineering/hardware evidence, without waiting on external recruitment that is not currently feasible.
- Phase 2's exit gate already independently requires "a repeated-use external cohort has measured activation, return rows, support burden, and willingness to pay" — that remains unchanged and is not a substitute for the Phase 4 design-partner item, since Phase 2's cohort is measuring solo-product retention/monetization, not first-use setup friction on the walking skeleton.
- Phase 4's exit gate grows one more item beyond the three ADR-0010 already added (managed-workout hardware runs, visual-performance evidence, realtime spike) plus ADR-0008/0009's signing and TCC items: named design partners attempting the supported setup unassisted, with categorized failures.
- The product-evidence gap this creates: nothing between Phase 0 and Phase 4 requires observing a real external user attempt setup unassisted. Phase 2's repeated-use cohort measures retention of people already onboarded, not first-use friction. This is an accepted, named risk, not an oversight.

## Alternatives considered

- **Keep it on Phase 1 as ADR-0010 decided:** rejected; recruitment is not feasible on the current timeline, and keeping it as a hard gate blocks Phase 1 from closing on a constraint engineering cannot resolve.
- **Fold it into Phase 2's existing repeated-use external cohort requirement:** rejected; Phase 2 measures retention/monetization of an already-onboarded cohort over weeks, not unassisted first-use setup friction on the walking skeleton — collapsing the two would lose the "no engineer driving the UI" signal until content/onboarding polish (Phase 2 scope) is already built, which is later than that signal is useful.
- **Remove it as a gate entirely, with no phase requiring it:** rejected; first-use-without-an-engineer evidence is real risk information the delivery plan has required since [the executive review](../architecture/00-executive-review.md), and Phase 4 already collects comparable hardware/rehearsal evidence before wider exposure, so it is a natural place to require it rather than dropping it.

## Validation and revisit

Revisit if design-partner recruitment becomes feasible before Phase 4 — nothing in this ADR discourages pulling the evidence forward, and doing so only strengthens the Phase 4 gate rather than requiring rework. Revisit if Phase 2 or Phase 3 stand up their own named external cohorts early enough that a separate Phase 4 recruitment effort becomes redundant; if so, a future ADR should reconcile which cohort's evidence satisfies which gate rather than requiring parallel recruitment efforts.
