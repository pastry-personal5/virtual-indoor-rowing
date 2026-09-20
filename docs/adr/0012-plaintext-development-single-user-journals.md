# ADR-0012: Plaintext development and single-user journals

- Status: Accepted
- Date: 2026-09-20
- Owners: CTO / Principal Architect, product owner

## Context

The Phase 1 internal application currently uses a Keychain-held key and
AES-256-GCM for local journal chunks and summaries. That adds Keychain prompts,
ad-hoc-build identity friction, and prevents direct inspection of an owner's
local run when diagnosing device, durability, or latency evidence. The present
scope is development and a single local user, not a multi-user or production
release.

## Decision

- From this decision forward, development and single-user local journals are
  owner-only plaintext SQLite/WAL data. They do not create, load, or require a
  Keychain data key, and do not encrypt/decrypt journal records.
- Technical latency aggregates are retained with their local session and are
  directly inspectable by the owner. They contain aggregates only, never raw
  PM packets, serial numbers, tokens, or heart-rate sequences.
- Existing encrypted journals are retained as legacy owner-local artifacts.
  They are neither rewritten nor deleted by this decision.
- Owner-only filesystem permissions remain required; FileVault remains
  recommended. Journal data remains private and must never be committed,
  attached to diagnostics without consent, or treated as anonymous.
- The encrypted per-profile Keychain design remains the required target for a
  future multi-user, shared-device, beta, or production scope. Reintroducing it
  requires a new scoped decision and migration/readback evidence.

## Consequences

- Development evidence can be inspected and correlated locally without a
  Keychain dependency.
- A copied development journal has weaker confidentiality protection than the
  prior design, so it is unsuitable as a release security posture.
- The durable-journal, offline-completion, ownership, and server-authoritative
  result boundaries are unchanged.
