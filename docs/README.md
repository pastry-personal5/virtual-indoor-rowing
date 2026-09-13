# Documentation

[ARCHITECTURE.md](../ARCHITECTURE.md) is the concise system reference. This directory holds the detailed record.

| Location | Purpose |
|---|---|
| [architecture/](architecture/) | Normative product and engineering specifications by concern |
| [adr/](adr/) | Architecture decisions; accepted decisions change only through a superseding ADR |
| [m1/](m1/) | Milestone 1 plan, interfaces, evidence, and changelog |
| [archive/research/](archive/research/) | Dated research evidence; not requirements |

Read the architecture documents in numeric order when a detailed decision is needed. The supported baseline is macOS Tahoe 26.6.2+ on Apple silicon; [ADR-0001](adr/0001-platform-and-toolchain.md) is authoritative.

## Milestone changelogs

Each milestone owns exactly one changelog at `docs/m<N>/CHANGELOG.md` (for example, `docs/m1/CHANGELOG.md`). It starts with `Unreleased`; accepted entries are dated, reverse-chronological, and grouped as Added, Changed, Fixed, Removed, or Security. Record material scope, interface, evidence, risk, or delivery-plan changes. Do not include generated files, routine formatting, secrets, serial numbers, or unredacted diagnostics.

A changelog records delivery history. It cannot alter a normative specification or reverse an accepted ADR.
