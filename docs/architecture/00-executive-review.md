# Executive product and architecture review

Status: Advisory baseline  
Owner: Founders / product / architecture  
Last reviewed: 2026-09-13

## Recommendation

Proceed, but treat the current design as a **target architecture**, not as permission to build the entire target at once. The strongest product claim is not “an Unreal rowing game”; it is a rowing experience that remains trustworthy when graphics, Bluetooth, network, or integrations fail. Preserve that differentiator.

The immediate company risk is scope rather than architectural feasibility. The candidate launch envelope contains device integration, a durable workout system, premium 3D content, accounts, synchronization, training analytics, social features, ranked racing, billing, third-party export, privacy operations, and operator tooling. A startup can design these boundaries together, but it should fund implementation one evidence gate at a time.

The recommended sequence is:

1. Prove that target rowers repeatedly choose the virtual experience over their current routine.
2. Prove PM5 setup, workout capture, and a single route are dependable without engineering assistance.
3. Add accounts and synchronization only after the local product earns repeated use.
4. Add private group rowing, then ranked racing, only after the telemetry and replay evidence is strong.
5. Automate commercial entitlement only when a pricing test shows that it is needed.

[ADR-0007](../adr/0007-evidence-gated-product-delivery.md) proposes making this sequencing an explicit product and architecture decision.

## Assessment

| Area | Assessment | Advisory action |
|---|---|---|
| Product promise | Differentiated and credible | Lead with workout trust, immersion, and continuity; do not lead with feature count |
| PM5 boundary | Strong | Keep the published-protocol, capability-matrix, and hardware-in-loop approach |
| Local durability | Strong with clarified failure semantics | Gate completion on durable commit; distinguish process crash from sudden power loss |
| Unreal boundary | Strong | Keep measurement and workout rules outside Actors, Blueprints, and rendering |
| Online architecture | Sound target, premature as a full build | Preserve contracts and seams; build only the control/API slice required by the active delivery phase |
| Ranked racing | Honest about the limits of BLE | Keep “ranked eligible” evidence-based; never market ordinary BLE as hardware attestation |
| Security/privacy | Production-oriented | Retain the controls, but implement them when the associated data or workflow first exists |
| Delivery plan | Technically sequenced but commercially under-gated | Add customer, retention, pricing, staffing, and runway gates to every investment step |
| Platform reach | Deliberately narrow and commercially risky | Treat M5 Max/Tahoe as a controlled validation cohort until a broader supported floor is measured |

## Product thesis and falsifiable hypotheses

The company should maintain one written product thesis:

> PM5-owning indoor rowers will row more consistently, and value the sessions more, when trustworthy Concept2 data drives an immersive, low-friction virtual route with useful training feedback.

That thesis contains hypotheses that can fail:

| Hypothesis | Cheapest useful evidence | Replan signal |
|---|---|---|
| Setup can be easier than the perceived value of the experience | Observed first-use sessions on the supported Mac/PM5 tuple | Repeated assisted pairing or permission recovery dominates the session |
| One excellent world is enough to create repeat use | Four-week cohort with one production route and structured workouts | Users try once but return to a non-virtual workflow |
| Trust and continuity are purchase drivers | Interview and behavior after offline/recovery demonstrations | Users do not notice or value the reliability advantage |
| Mac-only can recruit a viable initial cohort | Qualified lead funnel that records Mac/PM5 compatibility | Compatible prospects are too scarce or costly to acquire |
| Social/racing increases retention rather than novelty | Private scheduled-row experiment before ranked infrastructure | Participation does not improve repeat sessions or referrals |
| Customers will pay enough to support content and operations | Price/packaging interviews followed by a real checkout or paid pilot | Stated interest does not convert at a viable price |

Interview enthusiasm alone is not validation. Prefer observed setup, completed rows, voluntary returns, referrals, and payment behavior.

## Product scorecard

Use one scorecard from prototype onward. Segment it by new/returning rower, PM hardware/firmware, app build, and acquisition cohort without placing raw fitness data in general analytics.

| Measure | Definition | Why it matters |
|---|---|---|
| Qualified lead | Target user with a compatible or intentionally supported Mac and PM5 setup | Measures the real reachable market rather than general rowing interest |
| Activation | User reaches the first valid PM sample and completes a first intended row | Combines setup and core-value delivery |
| Successful row | Session reaches a user- or plan-directed end, is durably finalized, and has no critical data-loss flag | North-star unit of delivered value |
| Pairing success | Explicit pairing attempts reaching `Ready`, with time and failure category | Exposes the largest likely onboarding tax |
| Repeat rowing | Activated users completing another successful row in defined week-1 and week-4 windows | Tests habit value without optimizing time-in-app |
| Intended-workout completion | Started structured sessions reaching the plan/user-directed end; user stops are reported separately | Measures product usefulness without shaming safe stops |
| Trust failure | Lost, duplicated, misattributed, or materially incorrect session | Must be zero in release evidence; incidents are reviewed individually |
| Support burden | Assisted interventions and cases per 100 attempted sessions | Determines whether a narrow product can scale |
| Willingness to pay | Conversion and retention at an explicitly presented price/package | Prevents subscription infrastructure from substituting for pricing evidence |
| Contribution signal | Revenue less payment, cloud, support, and content-variable cost per active customer | Connects architecture and content cadence to a viable business |

Metric definitions, exclusions, consent, owner, query, and decision threshold belong in a versioned metric catalog before dashboards are used for a gate. Never redefine a denominator after seeing a weak result.

## Evidence-gated commitments

The functional table in [product scope](01-product-scope.md) is the first commercial-generation capability envelope. It is not one MVP backlog. Use these investment levels, mapped to the delivery phases in the [delivery plan](10-delivery-plan.md):

Each delivery phase is executed through one or more phase-scoped milestones and may contain many. Investment approval activates a delivery phase; milestone gates measure progress within it, while the phase exit gate decides whether its combined outcome is complete.

| Investment level | Delivery phase(s) | Outcome | Typical included capabilities | Investment gate |
|---|---|---|---|---|
| Foundation | Phases 0–1 | A reliable local rowing instrument and gray-box experience | PM5 connection, HUD, Just Row, journal/recovery, basic diagnostics | Toolchain, BLE, durability, and observed setup evidence |
| Solo product | Phase 2 | A product target users voluntarily repeat | One excellent route, workouts, history, FIT, accessibility, signed content | External cohort demonstrates activation and repeat rows |
| Connected beta | Phase 3 | Cross-device value without weakening offline use | Account claim, sync, catalog, privacy operations, optional Logbook | Local retention justifies cloud/data operational cost |
| Social/racing beta | Phase 4 | Scheduled rowing creates incremental value | Private groups, authoritative rooms, provisional/final results, operator controls | Private-row experiment plus deterministic race and load evidence |
| Commercial GA | Phase 5 | A supportable, monetized release | Proven packaging, entitlement where needed, on-call, legal and support readiness | Real pricing conversion, runway, SLO, and staffing evidence |

Do not begin a later investment level merely because its interfaces already exist. Interface foresight is inexpensive; production implementation and operations are not.

## First 90 days

Run product, device/client, and company-readiness work in parallel, with one integrated demonstration every two weeks.

### Days 0–30: remove existential uncertainty

- Recruit a named design-partner cohort from the actual supported hardware population.
- Observe current rowing/setup workflows; record alternatives, pain, frequency, buying process, and Mac/PM5 compatibility.
- Complete the toolchain, packaged-Bluetooth, PM5 telemetry, local-journal, and representative-scene spikes.
- Produce a gray-box end-to-end row whose PM display and recovered local summary can be compared.
- Decide launch market, company-owned Apple developer account, Concept2 contact path, and licensing owners.

### Days 31–60: prove the complete local loop

- Deliver an unsigned, ad-hoc-built internal build (ADR-0008) with explicit pairing, one route, live HUD, and durable summary.
- Test another app holding the PM, disconnect, renderer stall, app kill, offline use, and export (Bluetooth permission-denial rehearsal is deferred to Phase 4 per ADR-0009).
- Put the build in front of design partners without an engineer driving the UI.
- Instrument the scorecard with privacy-reviewed events and a manual experiment ledger where automation is premature.

### Days 61–90: decide whether to fund the solo product

- Run repeated rows with the same cohort over several weeks, not a one-session usability test.
- Measure activation, return behavior, trust failures, support burden, route appeal, and structured-workout demand.
- Present a real packaging and price proposition; distinguish politeness from commitment.
- Re-estimate delivery Phase 2 using measured content throughput, device defects, team availability, and cash runway.
- Accept, revise, or reject ADR-0007 and explicitly fund the next delivery phase. If evidence is weak, change the product or target cohort before building the cloud/race platform.

## Architecture guardrails for a lean startup

- Preserve the domain boundaries in the target design, but create deployables only when the active delivery phase needs them.
- Start the control plane as one modular Go process and one PostgreSQL database; do not provision every named managed service for the walking skeleton.
- Keep race contracts and deterministic rule fixtures engine-independent, but delay production gateways, leases, recovery, and multi-AZ load work until private group rows are approved.
- Build the PM simulator and local journal early because they shorten every later test cycle and protect the core promise.
- Keep one built-in route before building a generalized downloadable-content business.
- Prefer manual operator workflows with audited, least-privilege scripts during a tiny invited cohort; build a full console when volume and role separation require it.
- Treat the supported M5 Max/Tahoe setup as a validation constraint, not proof of a sufficient addressable market. Collect prospect hardware distribution before setting a broader minimum.
- Maintain a walking skeleton that can be installed and rowed at all times. Architecture documents are not progress if the end-to-end product cannot run.

## Stop or replan triggers

The founders should explicitly replan when any of these persists across a bounded experiment:

- compatible design partners cannot be recruited at a plausible acquisition cost;
- most first sessions require engineer intervention despite focused onboarding work;
- repeat rowing remains weak after the experience reaches the agreed usability/reliability bar;
- PM5 behavior or Unreal/toolchain packaging prevents a trustworthy supported tuple;
- one production-quality route costs more time or money than the business can repeat;
- price conversion cannot support content, support, payment, and infrastructure costs;
- the team cannot staff device reliability, client/content, and test ownership without making one a hidden part-time responsibility;
- ranked racing is asked to compensate for weak solo value rather than amplify proven engagement.

A replan is a successful use of evidence, not an architecture failure.

## Governance

- Review product evidence weekly and the risk register every two weeks through beta.
- Maintain one decision log with hypothesis, experiment, cohort, threshold, result, and decision.
- Record a named owner and expiry for every exception, assumption, remote kill switch, and postponed security/privacy control.
- Reforecast scope, staffing, runway, and target release date at each delivery-phase gate.
- Supersede accepted ADRs when evidence reverses them; do not allow a roadmap slide to silently override architecture.
