# Documentation

[ARCHITECTURE.md](../ARCHITECTURE.md) is the concise system reference. This directory holds the detailed, normative record.

| Location | Purpose |
|---|---|
| [architecture/](architecture/) | Normative product and engineering specifications by concern |
| [adr/](adr/) | Architecture decisions; accepted decisions change only through a superseding ADR |
| [phase-0/](phase-0/) | Completed diagnostic foundation and retained evidence |
| [phase-1/](phase-1/) | Completed walking-skeleton milestones and evidence |
| [phase-2/](phase-2/) | Planned solo-alpha packet; no milestone has started |
| [archive/research/](archive/research/) | Dated research evidence; not requirements |

Read the architecture documents in numeric order when a detailed decision is needed. Capitalized Phase 0–5 references use the [delivery-phase definitions](architecture/10-delivery-plan.md#delivery-phase-definitions). The supported baseline is macOS Tahoe 26.6.2+ on Apple silicon; [ADR-0001](adr/0001-platform-and-toolchain.md) is authoritative.

## Delivery records

Every milestone belongs to one delivery phase, uses `Phase <N> Milestone <M>`, and has a bounded gate. A phase passes only when its combined exit gate passes. Each phase has one `CHANGELOG.md`; it records delivery history, not architecture decisions. See the normative [delivery plan](architecture/10-delivery-plan.md).
