# ADR-0003: Purpose-built authoritative race service

- Status: Accepted
- Date: 2026-09-12
- Owners: Online lead, gameplay lead

## Context

Unreal offers authoritative client/server replication and dedicated servers, but VIR's competition authority is not a rendered physical world. It is an ordered stream of PM elapsed time/distance, a common race epoch, rowing-specific validation/rules, and rank. Course collision, water, animation, and camera are presentation only.

Running Unreal Linux dedicated servers would add engine source builds, large images/processes, engine networking coupling, and higher server cost for logic that does not need Actors or Chaos physics.

## Decision

- Build the real-time gateway and authoritative room worker in Go.
- Use binary Protobuf over WSS/443 from macOS client to stateless gateways; route to a single-owner room worker over internal mTLS gRPC.
- Run room rules at 20 Hz and snapshots at 10 Hz.
- Base accepted race progress on monotonic PM cumulative distance relative to the server-observed start baseline; never accept client avatar transforms.
- Keep the server world as route metadata/rules hashes, not a loaded Unreal map.
- Predict/interpolate visuals locally and reconcile to snapshots.
- Create deterministic replay and immutable result revisions as release requirements.

## Consequences

- Server processes are small, headless, portable Linux workloads and can host many rooms.
- The team owns time sync, socket protocol, gateway/worker routing, reconnect, interest/fanout, and replay tooling rather than using UE replication automatically.
- Client and server must share generated contracts and independent rules fixtures.
- Unreal world behavior cannot decide a result, which is intentional.
- A future more physics-interactive mode may need a different server model and new ADR.

## Alternatives considered

- **Unreal dedicated server:** strong general game solution but rejected for the launch race authority and cost profile.
- **Listen/peer host:** rejected for fairness, availability, NAT, and audit.
- **Stateless API + Redis on every 10 Hz frame:** rejected for hot-state contention and nondeterministic transaction cost.
- **API Gateway/Lambda per WebSocket message:** rejected for high-frequency cost/latency and room-state complexity.
- **UDP/QUIC custom transport at launch:** deferred; rowing cadence tolerates WSS and operational reach matters more. Protocol semantics permit later transport.

## Validation and revisit

Delivery Phase 0 proves two-client time sync/race/replay. Public beta requires load at twice forecast, room worker failure/fencing, deterministic results, and p99 tick ≤25 ms. Revisit for physics interactions, transport latency evidence, or when Go room logic can no longer represent approved rules.
