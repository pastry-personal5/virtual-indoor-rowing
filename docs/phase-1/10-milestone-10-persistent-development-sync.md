# Phase 1 Milestone 10: persistent development session sync

Status: Complete (automated implementation evidence); Compose and packaged-app follow-up evidence remain owner-run.

## Scope

This milestone replaces the Milestone 9 development stack's JSON/filesystem lifecycle projection with PostgreSQL session state and MinIO immutable objects. It stays opt-in, loopback-only, and is not an account, production, LAN, Terraform, or race-service implementation.

The existing versioned bootstrap/session API remains compatible. The API stores only hashed opaque bearer tokens, identity-confined lifecycle/idempotency rows, object metadata, and revisioned status. It grants a five-minute MinIO PUT URL scoped to the server-generated `development/<identity>/<session>.zst` object key. The API and worker use Compose-internal MinIO; the client-facing grant addresses MinIO only on host loopback. `make development-sync-up` applies the forward migration to both fresh and existing development volumes; `make development-sync-migrate` can repeat that migration explicitly.

Only finalized sessions can request an upload grant. The worker claims `processing` rows through PostgreSQL row locking and a 30-second lease, streams no more than 64 MiB from MinIO, verifies SHA-256 against the finalized session digest, bounds Zstandard expansion to 256 MiB, and requires the `SessionObject` protobuf envelope before converging duplicate/recovered work to `accepted_with_warnings` or `rejected`. Local finalization and outbox durability remain independent of this stack.

## Verification

- `go test ./...` in `Services/development-sync` covers canonical contract values, identity-confined keys, and the bounded worker limit.
- Follow-up owner-run Compose scenario: bootstrap, upload a deterministic object, restart API and worker while processing, and observe one stable revisioned status. This is retained evidence, not claimed as executed here.
- Packaged-app coordinator activation is deferred: it must be introduced only after a user-initiated journal/key access path can hand a worker its own cipher and SQLite connection without moving Keychain or network work onto the game thread. Real-PM5, latency, and process-kill Phase 1 exit evidence are unchanged.
