#include "LocalData/Schema.h"

namespace LocalData::Private
{
	namespace
	{
		bool IsMigrationApplied(FSqliteConnection &Connection, int Version)
		{
			auto Check = Connection.Prepare(
				"SELECT COUNT(*) FROM schema_migrations WHERE version = ?;");
			Check.BindInt64(1, Version);
			Check.Step();
			return Check.ColumnInt64(0) > 0;
		}

		void RecordMigration(FSqliteConnection &Connection, int Version, const char *Checksum)
		{
			auto Insert = Connection.Prepare(
				"INSERT INTO schema_migrations (version, checksum, applied_at) "
				"VALUES (?, ?, datetime('now'));");
			Insert.BindInt64(1, Version);
			Insert.BindText(2, Checksum);
			Insert.Step();
		}

		void ApplyVersion1(FSqliteConnection &Connection)
		{
			Connection.Execute("CREATE TABLE IF NOT EXISTS journal_events ("
							   "session_id TEXT NOT NULL,"
							   "sequence INTEGER NOT NULL,"
							   "monotonic_ns INTEGER NOT NULL,"
							   "kind TEXT NOT NULL,"
							   "payload_version INTEGER NOT NULL,"
							   "payload_blob BLOB,"
							   "PRIMARY KEY (session_id, sequence)"
							   ");");
			Connection.Execute("CREATE TABLE IF NOT EXISTS sample_chunks ("
							   "session_id TEXT NOT NULL,"
							   "first_sequence INTEGER NOT NULL,"
							   "last_sequence INTEGER NOT NULL,"
							   "codec TEXT NOT NULL,"
							   "crc32c INTEGER NOT NULL,"
							   "payload_blob BLOB NOT NULL,"
							   "PRIMARY KEY (session_id, first_sequence)"
							   ");");
			RecordMigration(Connection, 1, "v1-journal_events-sample_chunks");
		}

		// Adds the remaining docs/architecture/03-macos-unreal-client.md
		// "Minimum local tables". New tables key sessions by the 16-byte
		// FRowingSessionId blob; the v1 journal tables keep their TEXT
		// session_id unchanged so existing databases migrate without rewrites, which
		// is also why those two tables cannot carry a foreign key to sessions.
		void ApplyVersion2(FSqliteConnection &Connection)
		{
			Connection.Execute("CREATE TABLE IF NOT EXISTS sessions ("
							   "session_id BLOB PRIMARY KEY CHECK (length(session_id) = 16),"
							   "user_scope TEXT NOT NULL,"
							   "plan_id TEXT,"
							   "route_id TEXT,"
							   "state TEXT NOT NULL,"
							   "started_at_utc TEXT,"
							   "timezone TEXT,"
							   "source TEXT NOT NULL,"
							   "content_hash TEXT,"
							   "created_at TEXT NOT NULL"
							   ");");
			Connection.Execute("CREATE TABLE IF NOT EXISTS session_summaries ("
							   "session_id BLOB NOT NULL CHECK (length(session_id) = 16) REFERENCES sessions(session_id),"
							   "revision INTEGER NOT NULL,"
							   "metrics_blob BLOB NOT NULL,"
							   "quality_flags INTEGER NOT NULL,"
							   "finalized_at TEXT NOT NULL,"
							   "PRIMARY KEY (session_id, revision)"
							   ");");
			Connection.Execute("CREATE TABLE IF NOT EXISTS sync_outbox ("
							   "operation_id TEXT PRIMARY KEY,"
							   "aggregate_id BLOB NOT NULL,"
							   "kind TEXT NOT NULL,"
							   "attempt INTEGER NOT NULL DEFAULT 0,"
							   "next_attempt_at TEXT,"
							   "payload_hash TEXT NOT NULL"
							   ");");
			Connection.Execute("CREATE TABLE IF NOT EXISTS cloud_links ("
							   "session_id BLOB PRIMARY KEY CHECK (length(session_id) = 16) REFERENCES sessions(session_id),"
							   "cloud_id TEXT NOT NULL,"
							   "cloud_revision INTEGER NOT NULL,"
							   "acknowledged_at TEXT NOT NULL"
							   ");");
			Connection.Execute("CREATE TABLE IF NOT EXISTS installed_content ("
							   "content_id TEXT NOT NULL,"
							   "version TEXT NOT NULL,"
							   "manifest_hash TEXT NOT NULL,"
							   "status TEXT NOT NULL,"
							   "last_verified_at TEXT,"
							   "PRIMARY KEY (content_id, version)"
							   ");");
			Connection.Execute("CREATE TABLE IF NOT EXISTS paired_devices ("
							   "local_device_id TEXT PRIMARY KEY,"
							   "display_label TEXT NOT NULL,"
							   "capability_cache BLOB,"
							   "last_seen_at TEXT"
							   ");");
			RecordMigration(Connection, 2, "v2-sessions-summaries-outbox-links-content-devices");
		}

		void ApplyVersion3(FSqliteConnection &Connection)
		{
			Connection.Execute("ALTER TABLE sync_outbox ADD COLUMN state TEXT NOT NULL DEFAULT 'queued';");
			Connection.Execute("ALTER TABLE sync_outbox ADD COLUMN last_error TEXT;");
			RecordMigration(Connection, 3, "v3-sync-outbox-state-and-error");
		}

		void ApplyVersion4(FSqliteConnection &Connection)
		{
			Connection.Execute("ALTER TABLE journal_events ADD COLUMN codec TEXT NOT NULL DEFAULT 'raw-v1';");
			RecordMigration(Connection, 4, "v4-journal-event-codec");
		}

		void ApplyVersion5(FSqliteConnection &Connection)
		{
			Connection.Execute("CREATE TABLE IF NOT EXISTS session_latency_summaries ("
							   "session_id BLOB PRIMARY KEY CHECK (length(session_id) = 16) REFERENCES sessions(session_id),"
							   "schema_version INTEGER NOT NULL,"
							   "source_revision TEXT NOT NULL,"
							   "sample_count INTEGER NOT NULL,"
							   "retained_count INTEGER NOT NULL,"
							   "dropped_count INTEGER NOT NULL,"
							   "p50_ns INTEGER NOT NULL,"
							   "p95_ns INTEGER NOT NULL,"
							   "p99_ns INTEGER NOT NULL,"
							   "max_ns INTEGER NOT NULL,"
							   "recorded_at TEXT NOT NULL"
							   ");");
			RecordMigration(Connection, 5, "v5-session-latency-summaries");
		}

		void ApplyVersion6(FSqliteConnection &Connection)
		{
			Connection.Execute("ALTER TABLE session_summaries ADD COLUMN codec TEXT NOT NULL DEFAULT 'raw-v1+sealed';");
			RecordMigration(Connection, 6, "v6-session-summary-codec");
		}

		void ApplyVersion7(FSqliteConnection &Connection)
		{
			Connection.Execute("ALTER TABLE installed_content ADD COLUMN route_id TEXT;");
			Connection.Execute("ALTER TABLE installed_content ADD COLUMN catalog_revision INTEGER NOT NULL DEFAULT 0;");
			Connection.Execute("ALTER TABLE installed_content ADD COLUMN issued_at_unix_seconds INTEGER NOT NULL DEFAULT 0;");
			Connection.Execute("ALTER TABLE installed_content ADD COLUMN expires_at_unix_seconds INTEGER NOT NULL DEFAULT 0;");
			Connection.Execute("ALTER TABLE installed_content ADD COLUMN install_path TEXT;");
			Connection.Execute("ALTER TABLE installed_content ADD COLUMN failure_category TEXT;");
			Connection.Execute("CREATE UNIQUE INDEX installed_content_one_active "
							   "ON installed_content(status) WHERE status = 'active';");
			Connection.Execute("CREATE UNIQUE INDEX installed_content_one_last_known_good "
							   "ON installed_content(status) WHERE status = 'last_known_good';");
			Connection.Execute("CREATE TABLE content_catalog_state ("
							   "singleton INTEGER PRIMARY KEY CHECK (singleton = 1),"
							   "accepted_revision INTEGER NOT NULL,"
							   "manifest_hash TEXT NOT NULL,"
							   "accepted_at TEXT NOT NULL"
							   ");");
			Connection.Execute("CREATE TABLE content_downloads ("
							   "content_set_id TEXT PRIMARY KEY,"
							   "package_url TEXT NOT NULL,"
							   "staging_path TEXT NOT NULL,"
							   "expected_size_bytes INTEGER NOT NULL CHECK (expected_size_bytes > 0),"
							   "received_size_bytes INTEGER NOT NULL CHECK (received_size_bytes >= 0),"
							   "entity_tag TEXT NOT NULL,"
							   "updated_at TEXT NOT NULL"
							   ");");
			RecordMigration(Connection, 7, "v7-content-install-catalog-download-state");
		}

		// The unlocked check is only a fast path so read-only openers do not
		// take the write lock. It is repeated under BEGIN IMMEDIATE so two
		// connections opening the same older database cannot both apply it.
		template <typename FApply>
		void ApplyMigrationOnce(FSqliteConnection &Connection, int Version, FApply &&Apply)
		{
			if (IsMigrationApplied(Connection, Version))
			{
				return;
			}
			Connection.InTransaction([&]
									 {
				if (!IsMigrationApplied(Connection, Version))
				{
					Apply(Connection);
				} });
		}
	} // namespace

	void EnsureSchema(FSqliteConnection &Connection)
	{
		Connection.Execute("CREATE TABLE IF NOT EXISTS schema_migrations ("
						   "version INTEGER PRIMARY KEY,"
						   "checksum TEXT NOT NULL,"
						   "applied_at TEXT NOT NULL"
						   ");");

		ApplyMigrationOnce(Connection, 1, ApplyVersion1);
		ApplyMigrationOnce(Connection, 2, ApplyVersion2);
		ApplyMigrationOnce(Connection, 3, ApplyVersion3);
		ApplyMigrationOnce(Connection, 4, ApplyVersion4);
		ApplyMigrationOnce(Connection, 5, ApplyVersion5);
		ApplyMigrationOnce(Connection, 6, ApplyVersion6);
		ApplyMigrationOnce(Connection, 7, ApplyVersion7);
	}
} // namespace LocalData::Private
