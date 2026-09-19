# Phase 1 Milestone 9: local development session-sync vertical slice

Status: Implemented (local durability/contracts automated evidence); Compose end-to-end and packaged-app evidence remain owner-run

## Scope

This milestone adds an opt-in, loopback-only development synchronization seam. It does not add accounts, a production deployment, LAN access, Terraform, race authority, or Phase 3 synchronization.

`Contracts/openapi/development-sync-v1.yaml` defines versioned bootstrap and session lifecycle endpoints with RFC 9457 problems, opaque IDs, RFC 3339 instants, and decimal-safe digest fields. `rowing.v1.SessionObject` defines the immutable event/chunk/footer export envelope. The development API binds only to `127.0.0.1:8080`; it exchanges a Compose-generated bootstrap secret for a 24-hour opaque identity/token pair.

## Local durability boundary

Schema migrations 3 and 4 add sync-outbox retry state and journal-event codec markers. `FLocalDataJournalWriter::FinalizeSession` atomically commits the terminal event, `Ended` session state, encrypted summary, and a deduplicated `session_object_v1` outbox intent. It derives the outbox SHA-256 from the deterministic protobuf SessionObject compressed with Zstandard, including the prospective terminal event and summary. `ReadSessionObject` reconstructs those exact bytes for upload. `ReadJournalEvents` returns events in sequence order after local authenticated decryption, and `ReadPendingSyncOutbox` exposes only retry technical state. A failed finalization rolls back all writes; it cannot discard or change the active local workout.

The supplied Compose helper is intentionally optional. `make development-sync-bootstrap` creates the owner-only bootstrap secret, then `make development-sync-up` starts the loopback API and its PostgreSQL/MinIO development volumes. Neither simulator nor automation creates this configuration. `development-sync-reset` is narrowly scoped to that Compose project’s named volumes. Docker publishes the API only to host loopback; the container process listens on its internal interface so Docker forwarding works. `OnlineClient::FCoordinator` consumes only due durable-outbox work through a transport seam, renews the bootstrap token, uploads the exact reconstructed object, and records accepted/queued/needs-attention state; the native test uses a fake transport.

## Verification

- `local_data_tests`: `finalized_session_commits_terminal_summary_and_outbox_atomically` verifies terminal event/state/summary/outbox atomicity, encrypted ordered readback, and rollback when the session does not exist.
- `online_client_tests` verifies that a reconstructed object is uploaded and the durable outbox becomes accepted.
- `go test ./...` from `Services/development-sync` verifies restart-persisted bearer state, identity confinement, immutable-object digest acceptance, and rejection of non-versioned/invalid UUID or disposition requests.
- `make configure && cmake --build Build/native --target local_data_tests && ctest --test-dir Build/native -R local_data_tests --output-on-failure`
- Remaining before Phase 1 exit evidence: connecting the API persistence layer to PostgreSQL/MinIO, bounded streaming worker validation beyond this development digest projection, Compose end-to-end recovery, and the redacted packaged-app check. These are deliberately not claimed by this milestone’s automated evidence.
