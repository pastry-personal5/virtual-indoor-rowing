# Phased delivery plan

Status: Initial reference
Owner: CTO / product
Last reviewed: 2026-09-13

## Strategy

Build a trustworthy local rowing instrument first, then a polished solo game, then cloud synchronization, and only then ranked competition. Multiplayer assumptions are present in domain/contracts from the start, but public racing is not allowed to become the path by which basic device/session reliability is discovered.

Planning range for a focused startup team is roughly 8–12 months to a defensible public release. This is not a commitment until Phase 0 spikes measure toolchain, PM5, content, and staffing risk.

The range assumes the recommended team is staffed, external approvals do not block, and phases do not overlap beyond demonstrated capacity. At every exit gate, founders make a separate investment decision using product evidence, staffing, cost, runway, and remaining risk. Passing an engineering gate does not automatically fund the next phase. See the [executive review](00-executive-review.md) and proposed [ADR-0007](../adr/0007-evidence-gated-product-delivery.md).

## Recommended team

Minimum sustainable cross-functional team:

| Discipline | Initial allocation | Primary ownership |
|---|---:|---|
| Product/rowing domain | 1 product lead + fractional qualified coach | scope, workout content, acceptance, safety wording |
| Client/game | 2 senior Unreal C++ engineers | domain runtime, UI, world, performance |
| Device/macOS | 1 senior Apple/native engineer | CoreBluetooth, PM codec, signing/updater; can pair with client |
| Online/platform | 2 senior Go/cloud engineers | control/realtime/data/infrastructure |
| 3D/content | 1 technical artist + contract art/audio | route pipeline, environments, avatar, performance |
| Quality | 1 automation/SDET | simulator, HIL, end-to-end, load/release evidence |
| Design/research | 0.5–1 product designer | exertion UX, onboarding, accessibility, user research |
| Security/privacy/SRE | fractional early, named owner; increase pre-beta | threat/privacy reviews, incident/release/on-call |
| Support/community | add by private beta | device setup, diagnostics, event operations |

With fewer people, preserve the phase order and reduce content/social scope rather than making device durability, security, testing, or operations part-time invisible work.

## Phase 0 — evidence and foundation (2–4 weeks)

### Objectives

- Turn the highest-risk assumptions into working evidence.
- Establish source/build/contracts/test skeleton and accepted ADRs.
- Resolve external licensing/approval and exact version questions.

### Spikes

| Spike | Evidence required | Decision unlocked |
|---|---|---|
| Toolchain | Stock UE 5.8 Shipping app builds with Xcode 26.1.1 on Tahoe 26.6.2, runs arm64, signs/notarizes, Bluetooth permission works | confirm or revise ADR-0001/0006 |
| PM5 BLE | Discover/read/subscribe to real PM5s, 100 ms telemetry for 60 minutes, disconnect/reconnect captures, exact Model D/PM tuples | allowed capability seed and ADR-0002 |
| Managed workout | Configure/read back one distance, time, and interval workout using only current published CSAFE | launch managed-workout feasibility |
| Local durability | Append 10 Hz samples to chunked SQLite journal while rendering; kill at write boundaries and recover | journal parameters/ADR-0004 |
| Visual performance | One representative water route + one detailed boat/avatar at 2560×1600/60 fps for 60 minutes | render feature/content budgets |
| Realtime | Two load clients plus Go room loop; 20/10 Hz, time sync, reconnect, deterministic replay/result | ADR-0003 and protocol seed |
| Packaging/update | Update a prior signed/notarized test build through approved Sparkle version; corrupt feed/archive and rollback | update design acceptance |

### Business/external actions

- Recruit named design partners who own or regularly use the supported Mac/PM5 setup; observe their current setup and rowing workflow before prescribing UI.
- Define the product thesis, metric catalog, experiment log, target segment, alternatives, acquisition path, and a testable price/package proposition.
- Contact Concept2 developer relations: production use, protocol questions, qualification/authentication boundaries, Logbook write approval process, trademark/marketing review.
- Have counsel/finance review the current Unreal EULA and royalty plan. “Free-to-use” is not the same as royalty-free after commercial thresholds.
- Enroll and establish organizational Apple Developer ID ownership; no signing identity tied only to a founder's personal account.
- Review Sparkle, FIT SDK, Unreal/Fab/content/audio/font, Protobuf, SQLite, Zstandard, cloud, IdP, and billing licenses/terms.
- Choose launch markets and start privacy/subscription/consumer/accessibility/legal assessment for those markets.

### Exit gate

- All spikes have reproducible code/reports or an explicit decision to descope.
- Exact engine/Xcode/macOS/PM tuples are recorded.
- No unresolved blocker to BLE telemetry, notarization, or local session durability.
- Top risks have owners, triggers, and contingency.
- A compatible design-partner cohort is named, observed first-use evidence exists, and the founders have defined what customer evidence would fund Phase 1 and Phase 2.

## Phase 1 — walking skeleton (4–6 weeks)

### Deliverable

A signed internal macOS app connects to the PM5, rows a gray-box route, shows live metrics, journals every session, survives offline/cloud absence, and uploads to a minimal development API.

### Build order

1. Repository rules, pinned manifest, CI, stock engine, module boundaries.
2. `RowingCore` types/units/states and Protobuf contract generation.
3. PM simulator, protocol codec, CoreBluetooth adapter, capability handshake.
4. SQLite journal/chunks/recovery/outbox.
5. Minimal Unreal HUD, boat distance-to-spline, stroke animation states.
6. Control API session endpoint, PostgreSQL, object upload, worker validation.
7. Developer ID/sign/notarize internal pipeline and redacted diagnostics.

### Exit gate

- FR-002/003/004/007 demonstrated on real Model D/PM5 and simulator.
- 60-minute hardware run meets preliminary latency/durability; process-kill recovery evidence exists.
- Fresh install/permission denial/repair and signed package pass.
- No account, subscription, external integration, or multiplayer required to complete the row.
- Design partners can attempt the supported setup without an engineer driving the UI; failures are categorized and feed the Phase 2 estimate.

## Phase 2 — solo alpha and content pipeline (6–10 weeks)

### Scope

- Polished onboarding, settings, exertion HUD, accessibility and safe mode.
- Just Row routes, checkpoints/segments, one production-quality environment.
- Structured time/distance/interval plans and cues; PM managed mode where proven.
- Session summaries, history, personal bests, local ghosts, FIT export.
- Signed content pipeline, download/install/rollback, shader/PSO warm-up.
- Full PM simulator corpus, firmware matrix, HIL rig, thermal/performance gates.

### Exit gate

- Invited alpha users can install, pair, row for four weeks without engineering assistance for normal flows.
- QA-001/002/003/004/009/010/011/012 pass on the reference setup.
- At least 100 varied test workouts produce no unexplained loss/duplication and agree with PM display within resolution.
- Content validator and last-known-good/safe route are release-blocking.
- Training/safety copy has rowing-domain and product review.
- A repeated-use external cohort has measured activation, return rows, support burden, and willingness to pay. Founders explicitly approve or replan connected-product investment from that evidence.

## Phase 3 — accounts, sync, and private beta (6–10 weeks)

### Scope

- Managed OIDC/PKCE account, guest claim, Keychain, logout/user switch.
- Cloud history/samples, idempotent sync, reconciliation, data export/deletion.
- Catalog, entitlement abstraction, hosted billing sandbox then production readiness.
- Concept2 Logbook OAuth and approved development writes; delivery status/retry.
- Friends/block/privacy and coarse presence, without text/voice/UGC.
- Operations console, support case/diagnostic flow, observability, backup/restore.
- Direct distribution beta/update channels and staged rollout.

### Exit gate

- Account/sync/integration/provider outages never block local completion.
- Authorization negative suite, privacy export/deletion synthetic test, external token protection, and log redaction pass.
- Concept2 production write approval exists before live export is enabled.
- Restore, rollback, incident/on-call/support runbooks rehearsed.
- Private beta crash-free session rate ≥99.5% and actionable device/support taxonomy exists.

## Phase 4 — real-time group rows and ranked racing (8–12 weeks)

### Scope

- Gateway, matchmaker, room worker, room lease/recovery, WSS protocol.
- Private join-code group row, bounded presence, emotes, spectator role.
- Synchronized countdown, fixed-distance ranked races, penalties/DNF/DNS.
- Server-authoritative accepted distance/rank, prediction/reconciliation.
- Provisional/final result, immutable revision, leaderboard, integrity evidence/review.
- Load client, deterministic replay, latency preflight, regional warning, fault/load/soak.
- Event scheduling/operator tools for small supervised community events.

Start with private unranked rooms, then opt-in unranked public, then ranked canary. Do not expose ranked results until deterministic replay and result finalization are release gates.

### Exit gate

- Initial 5,000-connection/1,000-active planning load and configured room maxima pass at 2× forecast with SLO headroom.
- Worker crash/lease fencing/recovery or safe cancellation is demonstrated under load.
- Common-start and finish order agree across controlled latency cases and replay.
- Integrity labels/appeal/sanction policies are reviewed and avoid “hardware proof” claims.
- Production on-call, event operator, capacity, and kill-switch rehearsals pass.

## Phase 5 — public beta to GA (6–8 weeks)

### Scope

- Reliability/thermal/accessibility/security/privacy fixes; no architecture-scale new features.
- Broaden real PM5 hardware/firmware evidence within the agreed launch boundary.
- External penetration test and native parser/update-focused review.
- Subscription/support/customer communications and operational readiness.
- Performance/content polish, localization for chosen markets, funnel/support learning.
- Disaster restore, provider outage, signing/update revocation, and race incident drills.

### GA gate

- The full evidence bundle in [verification strategy](09-verification-strategy.md) is approved.
- Crash-free session objective ≥99.8% over a representative beta population/period.
- 99.9% online SLO evidence or an explicitly revised public promise.
- No open blocker in workout loss/corruption, authz/privacy, update signing, race result correctness, physical safety, or required legal licensing/approval.
- Staffing/on-call/support/content cadence and cash/cloud forecasts cover at least the next release cycle.

## Post-GA candidates

Prioritize from measured retention/support/demand:

- second race region and explicit data-home/multi-region control-plane design;
- training plans/FTP and deeper analytics;
- more routes, avatar customization, pace partners, challenges/achievements;
- additional fitness integrations;
- iPhone/watch companion for Apple Health rather than attempting HealthKit on macOS;
- PM3/PM4/PM5 USB adapter after macOS/HID/driver, cable UX, and HIL review;
- other rower protocols behind the same device boundary;
- Apple Store distribution/StoreKit variant;
- wider Apple-silicon performance matrix, then other platforms;
- certified/supervised high-stakes event mode.

Each candidate begins with product evidence and a small ADR/threat/privacy/test update. Cross-platform work must not push platform types into `RowingCore`.

## Build versus buy

| Capability | Choice | Boundary |
|---|---|---|
| 3D runtime/editor | Unreal Engine | pin stock version; domain logic remains portable C++ |
| BLE | Apple CoreBluetooth | thin Objective-C++ adapter; own Concept2 codec |
| Identity | managed OIDC/Cognito initially | standard OIDC and internal user mapping |
| Billing | hosted provider | internal entitlement ledger/provider adapter |
| Updates | Sparkle 2 + Apple signing/notarization | wrapper and independent signed feed/archive |
| Primary data/cache/queue/object | managed AWS services | PostgreSQL/Redis/SQS/S3 interfaces and Terraform |
| Realtime race rules | build | core differentiator and trust authority |
| Product/device/session domain | build | core differentiator and reliability boundary |
| Observability | OpenTelemetry plus managed backends | portable signals and vendor adapter |
| Content | own/commission/license | provenance and validation required |

Avoid buying a generic MMO backend: the hard domain is ordered PM measurements, workout continuity, and rowing-specific result integrity, while world physics is presentation-only.

## Risk register

| Risk | Probability / impact | Early signal | Mitigation / contingency | Owner |
|---|---|---|---|---|
| Tahoe/Xcode/UE incompatibility | M / H | compile/link/package/notary or render defect | pin proven trio; stock patch; test newer toolchain separately; defer OS bump | client/release |
| PM5 firmware/hardware fragmentation | H / H | layout/property/state divergence | capability matrix, real-device fleet, published-spec parser, block/warn; reduce supported tuples | device |
| BLE contention/dropout at rower | H / H | gaps/reconnect/support volume | explicit pairing, other-app guidance, 100 ms soak/interference tests, durable quality flags; later USB option | device/UX |
| Managed workout control unreliable | M / H | readback/timeouts/PM bad state | transaction state machine; app-guided fallback for solo; disallow ranked; coordinate Concept2 | device/product |
| Workout loss/corruption | M / Critical | recovery mismatch/duplicate | append journal, checksummed chunks, kill tests, outbox/idempotency; stop launch | client/data |
| Mac thermal/render hitch | M / H | 60-min p95/hitches fail | conservative Metal feature baseline, budgets/validator, dynamic scalability, smaller launch world | graphics/client |
| Race fraud/false claims | H / H | plausible synthetic devices/anomaly appeals | bounded labels, server rules/evidence/review; separate supervised events | integrity/product |
| Global latency from Seoul | H / M | >150 ms p95 preflight share | warn/unrank; second race region trigger; no premature multi-region account writes | online/product |
| Realtime cost/fanout | M / M | egress/tick/custom metric trend | caps/interest/delta, load/cost tests, dedicated workers at trigger | online/platform |
| Content production dominates schedule | H / M | route misses budgets/milestones | one excellent route, reusable kits/procedural tooling, outsource with validator | content/product |
| Concept2 API/protocol/trademark approval | M / H | unanswered approval or production-write block | engage Phase 0; keep export optional; published-only protocol; neutral branding | business/device |
| Unreal commercial cost/terms | L–M / H | revenue/funding/distribution changes | legal/finance review, royalty reporting owner, forecast; custom license if justified | CEO/finance |
| Update/signing key compromise | L / Critical | unauthorized release/key access | isolated keys, two-person approval, transparency, rotation/revocation drill | security/release |
| Privacy/subscription market obligations | M / H | late counsel/store/support requirements | choose markets early, minimize data, provider checkout, policy/flows before beta | product/legal |
| Startup team over-scope | H / H | parallel unfinished systems, slipping reliability | phase gates, deferred list, one route/PM/platform, feature freeze before GA | CTO/product |

Risk status is reviewed every two weeks through beta. “Mitigated” requires evidence, not work started.

## Decision and scale triggers

- **Extract control microservice:** two teams need independent release/ownership or a module's load/failure materially harms others; first use transactional outbox, not distributed transactions.
- **Dedicated race compute:** p99 tick >25 ms/scheduling jitter at valid forecast with optimized code, or lower stable cost at sustained utilization.
- **Second race region:** agreed percentage of race-ready users repeatedly fails regional p95 ≤150 ms and demand supports operations.
- **XPC device helper:** renderer/process failures threaten workout capture despite queue/journal controls, with a proven helper lifecycle/signing prototype.
- **USB support:** BLE-caused failed-session/support rate exceeds threshold and cable/native USB spike proves better user outcome.
- **Engine fork:** reproducible blocking engine defect with no configuration/project workaround and maintainable isolated patch.
- **Mac App Store:** acquisition/trust upside exceeds review/sandbox/billing/update/content constraints, proven with a separate package spike.
- **Time-series database:** product queries cannot be met by summary projections plus object/Athena processing within SLO/cost.
- **Open chat/UGC:** moderation, reporting, blocking, retention, staffing, and market safety work is funded before implementation.

## Product metrics

Measure outcomes without collecting unnecessary fitness detail:

- pairing success and time-to-first-valid-sample by capability/error class;
- first completed workout, crash-free/session-save/sync success;
- weekly completed rows and return rate (with approved analytics consent/model);
- structured-workout completion and user-chosen stop, without shaming;
- race join/start/finish/reconnect/cancel and latency distribution;
- support cases per 100 sessions and top actionable device/setup causes;
- update adoption/failure/rollback;
- external export success and user-repair rate;
- content route performance/usage and download failures;
- infrastructure cost per completed session and active race hour.

Do not optimize raw time-in-app: the product should help users complete an intended workout efficiently and safely.

## Architecture completion checklist

Before calling the initial architecture implemented:

- Every accepted ADR has code/config/test evidence or is superseded.
- Every `FR-*` and `QA-*` maps to release evidence.
- Domain boundaries and dependency checks exist in build/review tooling.
- PM support matrix comes from HIL evidence and can be safely tightened.
- Offline completion, crash recovery, idempotent sync, and race replay are automated gates.
- Signing/notarization/update/content chain is rehearsed from last stable.
- Backups restore, incidents alert, runbooks work, and owners/on-call exist.
- Privacy inventory/retention/export/deletion and external processor map match production.
- Licensing, Concept2 production access/branding, Apple organization, and launch-market reviews are signed off.
