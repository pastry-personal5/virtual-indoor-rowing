# ADR-0004: Offline-first session journal and idempotent synchronization

- Status: Accepted
- Date: 2026-09-12
- Owners: Client lead, data lead

## Context

A workout may last an hour while Wi-Fi, cloud, renderer, application, or external providers fail. Saving only a final summary or streaming the only copy to a server makes exercise data vulnerable. At the same time, high-frequency samples do not belong as individual synchronous SQL writes on the game thread.

## Decision

- Create `session_id` on the client before workout preparation.
- Append versioned session events/normalized samples to local SQLite in WAL mode through a dedicated writer.
- Store samples in one-second independently checksummed compressed chunks; transactionally commit final summary and an outbox item.
- Treat `Completed`, `Interrupted`, and `Aborted` distinctly and mark unclean recovery/gaps.
- Synchronize through idempotent metadata plus a digest-verified compressed session object.
- Retain local data until explicit cloud acknowledgement and user retention action.
- Make cloud/provider jobs at-least-once with idempotent consumers.
- Never let a race/provider result overwrite PM source facts; link revisions and reconcile.

## Consequences

- Solo workouts work without an account/network and provider outages are isolated.
- Local schema migration, corruption recovery, disk pressure, user switching, and duplicate reconciliation require serious tests.
- The client temporarily holds fitness data and must provide privacy/export/delete behavior.
- Session/event versioning is required from the first vertical slice.
- “Exactly once” is achieved as an observable effect through idempotency/uniqueness, not assumed from message delivery.

## Alternatives considered

- **Cloud-only streaming:** rejected because connectivity becomes workout durability.
- **Write final JSON file only:** rejected because crashes lose the session and partial corruption is broad.
- **One SQLite row per callback synchronously:** rejected because it couples device/frame timing and creates avoidable overhead.
- **Store every dense sample forever in PostgreSQL:** rejected for cost/query/retention shape; summary tables plus immutable objects are more suitable.

## Validation and revisit

Kill the process at every write boundary, inject disk full/corrupt tail/duplicates, and prove stated loss/recovery behavior. Revisit chunk size/synchronous mode only from device latency and durability benchmarks, while preserving the event and idempotency model.
