# ADR-0007: Evidence-gated product delivery

- Status: Proposed
- Date: 2026-09-13
- Owners: CEO/product, CTO

## Context

The first-generation capability envelope spans a trustworthy local workout instrument, a premium Unreal experience, accounts and synchronization, content delivery, social features, authoritative racing, integrations, billing, privacy workflows, and operator tooling. Their boundaries should be designed coherently, but implementing and operating them together would expose a startup to scope, staffing, runway, and market risk before repeat use or willingness to pay is proven.

Technical exit gates already exist. An investment decision also needs customer and commercial evidence. Otherwise a phase can be technically successful while funding the wrong product.

## Proposed decision

- Treat the architecture documents as the target shape for the first commercial product generation, not as one concurrently committed backlog.
- Fund work in five commitment levels: Foundation, Solo product, Connected beta, Social/racing beta, and Commercial GA.
- Require an explicit investment review between levels using product behavior, qualitative evidence, reliability/support data, staffing, cost, and runway.
- Keep later domain seams and contract compatibility in the design, but do not build production infrastructure, operational controls, or generalized frameworks before their level is approved.
- Protect the non-negotiable core at every level: PM measurement authority, durable local completion, engine-independent domain rules, privacy by default, and honest integrity labels.
- Treat customer research, design-partner recruitment, onboarding observation, repeat use, and pricing evidence as delivery work with named owners and artifacts.
- Reforecast schedule and scope at each gate. Date pressure does not permit silently weakening workout durability, authorization, update trust, or result correctness.

## Consequences

- The company can stop, narrow, or change direction before cloud/race/commercial complexity becomes sunk cost.
- Some target interfaces and tests will exist before their production adapters; this is intentional.
- Roadmaps must distinguish capability envelope, approved phase, experiment, and committed release.
- Founders must make explicit investment decisions rather than treating completion of engineering tasks as product validation.
- A feature may be architecturally designed yet remain unfunded and unavailable.

## Alternatives considered

- **Build the full candidate launch scope in parallel:** rejected because it maximizes integration and operating surface before retention and pricing evidence.
- **Prototype without durable/domain foundations:** rejected because throwaway device/session behavior would test an experience that cannot support the product promise.
- **Build racing first as the differentiator:** deferred until a private scheduled-row experiment proves social value and the measurement pipeline is dependable.
- **Avoid all future-facing interfaces:** rejected because small, stable seams around devices, persistence, identity, and race contracts are inexpensive insurance against avoidable rewrites.

## Acceptance and revisit

This ADR remains Proposed until the founders approve the commitment model and name the phase-gate decision makers. If accepted, update the roadmap and work tracker so every item declares its commitment level and active gate.

Revisit after the first repeated-use cohort, a material target-market change, a financing/staffing change, or evidence that social/racing must precede solo retention. Supporting guidance lives in the [executive review](../architecture/00-executive-review.md).
