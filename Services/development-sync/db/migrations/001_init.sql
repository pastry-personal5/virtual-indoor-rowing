CREATE TABLE IF NOT EXISTS development_identities (
    identity_id TEXT PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS development_tokens (
    token_sha256 TEXT PRIMARY KEY CHECK (token_sha256 ~ '^[a-f0-9]{64}$'),
    identity_id TEXT NOT NULL REFERENCES development_identities(identity_id),
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE TABLE IF NOT EXISTS idempotency_keys (
    identity_id TEXT NOT NULL REFERENCES development_identities(identity_id),
    idempotency_key TEXT NOT NULL,
    request_sha256 TEXT NOT NULL,
    response_json JSONB NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (identity_id, idempotency_key)
);
CREATE TABLE IF NOT EXISTS sessions (
    session_id UUID NOT NULL,
    identity_id TEXT NOT NULL REFERENCES development_identities(identity_id),
    disposition TEXT NOT NULL CHECK (disposition IN ('completed', 'interrupted', 'aborted')),
    object_sha256 TEXT NOT NULL CHECK (object_sha256 ~ '^[a-f0-9]{64}$'),
    status TEXT NOT NULL,
    revision BIGINT NOT NULL DEFAULT 1,
    processing_lease_until TIMESTAMPTZ,
    processing_lease_owner TEXT,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (identity_id, session_id)
);
CREATE INDEX IF NOT EXISTS sessions_processing_idx ON sessions (status, processing_lease_until)
    WHERE status = 'processing';
CREATE TABLE IF NOT EXISTS session_objects (
    identity_id TEXT NOT NULL,
    session_id UUID NOT NULL,
    object_sha256 TEXT NOT NULL,
    object_key TEXT NOT NULL,
    size_bytes BIGINT NOT NULL,
    status TEXT NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT now(),
    PRIMARY KEY (identity_id, session_id),
    FOREIGN KEY (identity_id, session_id) REFERENCES sessions(identity_id, session_id)
);
