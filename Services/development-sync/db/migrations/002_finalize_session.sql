-- Forward-only development migration. Safe on fresh and existing volumes.
ALTER TABLE sessions ADD COLUMN IF NOT EXISTS finalized_at TIMESTAMPTZ;
