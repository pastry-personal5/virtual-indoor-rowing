#include "LocalData/LocalDataJournal.h"

#include "LocalData/Crc32c.h"
#include "LocalData/Schema.h"
#include "LocalData/Sqlite.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace LocalData
{
	namespace
	{
		using Private::ComputeCrc32c;
		using Private::FSqliteConnection;

		struct FChunkRow
		{
			std::string SessionId;
			std::uint64_t FirstSequence = 0;
			std::uint64_t LastSequence = 0;
			std::uint64_t StoredChecksum = 0;
			std::string PayloadBlob;
		};

		std::vector<FChunkRow> LoadChunkRows(FSqliteConnection &Connection)
		{
			std::vector<FChunkRow> Rows;
			auto Statement = Connection.Prepare(
				"SELECT session_id, first_sequence, last_sequence, crc32c, "
				"payload_blob FROM sample_chunks "
				"ORDER BY session_id, first_sequence;");
			while (Statement.Step())
			{
				FChunkRow Row;
				Row.SessionId = Statement.ColumnText(0);
				Row.FirstSequence =
					static_cast<std::uint64_t>(Statement.ColumnInt64(1));
				Row.LastSequence =
					static_cast<std::uint64_t>(Statement.ColumnInt64(2));
				Row.StoredChecksum =
					static_cast<std::uint64_t>(Statement.ColumnInt64(3));
				Row.PayloadBlob = Statement.ColumnBlob(4);
				Rows.push_back(std::move(Row));
			}
			return Rows;
		}

		void DeleteChunk(FSqliteConnection &Connection,
						 const std::string &SessionId,
						 std::uint64_t FirstSequence)
		{
			Connection.BeginImmediate();
			auto Statement = Connection.Prepare(
				"DELETE FROM sample_chunks WHERE session_id = ? AND "
				"first_sequence = ?;");
			Statement.BindText(1, SessionId);
			Statement.BindInt64(2, static_cast<std::int64_t>(FirstSequence));
			Statement.Step();
			Connection.Commit();
		}

		// True if no terminal event (Completed/Interrupted/Aborted) was ever
		// committed for SessionId. Checked against the highest-sequence
		// event rather than just "the last event is Started" so this stays
		// true on a repeated scan after a RecoveredAfterUncleanExit marker
		// has already been recorded (idempotency).
		bool SessionLacksTerminalEvent(FSqliteConnection &Connection,
									   const std::string &SessionId)
		{
			auto Statement = Connection.Prepare(
				"SELECT kind FROM journal_events WHERE session_id = ? "
				"ORDER BY sequence DESC LIMIT 1;");
			Statement.BindText(1, SessionId);
			if (!Statement.Step())
			{
				return false;
			}
			const std::string Kind = Statement.ColumnText(0);
			return Kind != "Completed" && Kind != "Interrupted" &&
				   Kind != "Aborted";
		}

		bool HasRecoveryMarker(FSqliteConnection &Connection,
							   const std::string &SessionId)
		{
			auto Statement = Connection.Prepare(
				"SELECT COUNT(*) FROM journal_events WHERE session_id = ? "
				"AND kind = 'RecoveredAfterUncleanExit';");
			Statement.BindText(1, SessionId);
			Statement.Step();
			return Statement.ColumnInt64(0) > 0;
		}

		void RecordRecoveryMarker(FSqliteConnection &Connection,
								  const std::string &SessionId)
		{
			auto NextSequenceStatement = Connection.Prepare(
				"SELECT COALESCE(MAX(sequence), 0) + 1 FROM journal_events "
				"WHERE session_id = ?;");
			NextSequenceStatement.BindText(1, SessionId);
			NextSequenceStatement.Step();
			const auto NextSequence = NextSequenceStatement.ColumnInt64(0);

			Connection.BeginImmediate();
			auto Insert = Connection.Prepare(
				"INSERT INTO journal_events "
				"(session_id, sequence, monotonic_ns, kind, payload_version, "
				"payload_blob) VALUES (?, ?, 0, "
				"'RecoveredAfterUncleanExit', 1, '');");
			Insert.BindText(1, SessionId);
			Insert.BindInt64(2, NextSequence);
			Insert.Step();
			Connection.Commit();
		}
	} // namespace

	FLocalDataRecoveryReport
	ScanAndRecover(const std::filesystem::path &DatabasePath)
	{
		FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);

		FLocalDataRecoveryReport Report;
		std::optional<std::string> CurrentSessionId;
		std::optional<std::uint64_t> LastGoodLastSequence;

		for (const FChunkRow &Row : LoadChunkRows(Connection))
		{
			if (!CurrentSessionId.has_value() || *CurrentSessionId != Row.SessionId)
			{
				CurrentSessionId = Row.SessionId;
				LastGoodLastSequence.reset();
			}

			// Deduplicate: this row's range overlaps a range already kept
			// for this session.
			if (LastGoodLastSequence.has_value() &&
				Row.FirstSequence <= *LastGoodLastSequence)
			{
				DeleteChunk(Connection, Row.SessionId, Row.FirstSequence);
				++Report.DuplicateChunkCount;
				continue;
			}

			const std::uint32_t RecomputedChecksum =
				ComputeCrc32c(Row.PayloadBlob);
			if (RecomputedChecksum != Row.StoredChecksum)
			{
				// Truncate-incomplete-tail: drop only this row, not the
				// whole session; earlier verified chunks stay committed.
				DeleteChunk(Connection, Row.SessionId, Row.FirstSequence);
				++Report.TruncatedChunkCount;
				continue;
			}

			LastGoodLastSequence = Row.LastSequence;
			Report.HighestVerifiedSequence =
				std::max(Report.HighestVerifiedSequence, Row.LastSequence);
		}

		if (CurrentSessionId.has_value() &&
			SessionLacksTerminalEvent(Connection, *CurrentSessionId))
		{
			Report.RecoveredAfterUncleanExit = true;
			if (!HasRecoveryMarker(Connection, *CurrentSessionId))
			{
				RecordRecoveryMarker(Connection, *CurrentSessionId);
			}
		}

		return Report;
	}
} // namespace LocalData
