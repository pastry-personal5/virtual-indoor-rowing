#include "LocalData/Schema.h"

namespace LocalData::Private
{
	namespace
	{
		constexpr int SchemaVersion = 1;
	}

	void EnsureSchema(FSqliteConnection &Connection)
	{
		Connection.Execute("CREATE TABLE IF NOT EXISTS schema_migrations ("
						   "version INTEGER PRIMARY KEY,"
						   "checksum TEXT NOT NULL,"
						   "applied_at TEXT NOT NULL"
						   ");");

		auto Check = Connection.Prepare(
			"SELECT COUNT(*) FROM schema_migrations WHERE version = ?;");
		Check.BindInt64(1, SchemaVersion);
		Check.Step();
		if (Check.ColumnInt64(0) > 0)
		{
			return;
		}

		Connection.BeginImmediate();
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
		auto Insert = Connection.Prepare(
			"INSERT INTO schema_migrations (version, checksum, applied_at) "
			"VALUES (?, ?, datetime('now'));");
		Insert.BindInt64(1, SchemaVersion);
		Insert.BindText(2, "v1-journal_events-sample_chunks");
		Insert.Step();
		Connection.Commit();
	}
} // namespace LocalData::Private
