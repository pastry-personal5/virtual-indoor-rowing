# Documentation

[ARCHITECTURE.md](../ARCHITECTURE.md) is the concise system reference. This directory holds the detailed record.

| Location | Purpose |
|---|---|
| [architecture/](architecture/) | Normative product and engineering specifications by concern |
| [adr/](adr/) | Architecture decisions; accepted decisions change only through a superseding ADR |
| [phase-0/](phase-0/) | Bounded diagnostic milestones contributing to delivery Phase 0 |
| [archive/research/](archive/research/) | Dated research evidence; not requirements |

Read the architecture documents in numeric order when a detailed decision is needed. Capitalized Phase 0–5 references use the [delivery-phase definitions](architecture/10-delivery-plan.md#delivery-phase-definitions). The supported baseline is macOS Tahoe 26.6.2+ on Apple silicon; [ADR-0001](adr/0001-platform-and-toolchain.md) is authoritative.

## Delivery phases, milestones, and changelogs

Every delivery phase must contain one or more milestones and may contain many. Every milestone belongs to exactly one delivery phase, is named `Phase <N> Milestone <M>`, and is documented inside `docs/phase-<N>/`. Milestone numbers are scoped to their owning phase.

Each delivery phase owns one `docs/phase-<N>/CHANGELOG.md` covering all of its milestones. Every changelog entry identifies the affected milestone. The changelog starts with `Unreleased`; accepted entries are dated, reverse-chronological, and grouped as Added, Changed, Fixed, Removed, or Security. Record material scope, interface, evidence, risk, or delivery-plan changes. Do not include generated files, routine formatting, secrets, serial numbers, or unredacted diagnostics.

A milestone gate evaluates only that milestone. A delivery-phase exit gate evaluates the combined required outcomes and evidence from its milestones. Completing any one milestone does not complete the phase. A changelog records delivery history; it cannot alter a normative specification or reverse an accepted ADR.
