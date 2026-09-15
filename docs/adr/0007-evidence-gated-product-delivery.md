# ADR-0007: Evidence-gated product delivery

- Status: Proposed
- Date: 2026-09-13
- Owners: CEO/product, CTO

## Context

The first-generation capability envelope spans a trustworthy local workout instrument, a premium Unreal experience, accounts and synchronization, content delivery, social features, authoritative racing, integrations, billing, privacy workflows, and operator tooling. Their boundaries should be designed coherently, but implementing and operating them together would expose a startup to scope, staffing, runway, and market risk before repeat use or willingness to pay is proven.

Technical exit gates already exist. An investment decision also needs customer and commercial evidence. Otherwise a delivery phase can be technically successful while funding the wrong product.

## Proposed decision

- Treat the architecture documents as the target shape for the first commercial product generation, not as one concurrently committed backlog.
- Fund work in five investment levels: Foundation (delivery Phases 0–1), Solo product (Phase 2), Connected beta (Phase 3), Social/racing beta (Phase 4), and Commercial GA (Phase 5).
- Decompose every delivery phase into one or more phase-scoped milestones. A phase may have many milestones, but a milestone belongs to exactly one phase and its completion does not pass the phase exit gate.
- Require an explicit investment review between levels using product behavior, qualitative evidence, reliability/support data, staffing, cost, and runway.
- Keep later domain seams and contract compatibility in the design, but do not build production infrastructure, operational controls, or generalized frameworks before their level is approved.
- Protect the non-negotiable core at every level: PM measurement authority, durable local completion, engine-independent domain rules, privacy by default, and honest integrity labels.
- Treat customer research, design-partner recruitment, onboarding observation, repeat use, and pricing evidence as delivery work with named owners and artifacts.
- Reforecast schedule and scope at each gate. Date pressure does not permit silently weakening workout durability, authorization, update trust, or result correctness.

## Consequences

- The company can stop, narrow, or change direction before cloud/race/commercial complexity becomes sunk cost.
- Some target interfaces and tests will exist before their production adapters; this is intentional.
- Roadmaps must distinguish capability envelope, approved delivery phase, experiment, and committed release.
- Founders must make explicit investment decisions rather than treating completion of engineering tasks as product validation.
- A feature may be architecturally designed yet remain unfunded and unavailable.

## Alternatives considered

- **Build the full candidate launch scope in parallel:** rejected because it maximizes integration and operating surface before retention and pricing evidence.
- **Prototype without durable/domain foundations:** rejected because throwaway device/session behavior would test an experience that cannot support the product promise.
- **Build racing first as the differentiator:** deferred until a private scheduled-row experiment proves social value and the measurement pipeline is dependable.
- **Avoid all future-facing interfaces:** rejected because small, stable seams around devices, persistence, identity, and race contracts are inexpensive insurance against avoidable rewrites.

## Acceptance and revisit

This ADR remains Proposed until the founders approve the investment model and name the delivery-phase-gate decision makers. If accepted, update the roadmap and work tracker so every item declares its investment level and active gate.

Revisit after the first repeated-use cohort, a material target-market change, a financing/staffing change, or evidence that social/racing must precede solo retention. Supporting guidance lives in the [executive review](../architecture/00-executive-review.md).
