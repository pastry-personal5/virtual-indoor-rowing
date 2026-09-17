#include "LocalData/LocalDataJournal.h"

#include "LocalData/Crc32c.h"
#include "LocalData/SampleChunkCodec.h"
#include "LocalData/Schema.h"
#include "LocalData/Sqlite.h"

namespace LocalData
{
	namespace
	{
		using LocalData::Private::ComputeCrc32c;
		using LocalData::Private::EncodeSamples;
		using LocalData::Private::FSqliteConnection;

		std::string JournalEventKindToString(EJournalEventKind Kind)
		{
			switch (Kind)
			{
			case EJournalEventKind::Started:
				return "Started";
			case EJournalEventKind::Completed:
				return "Completed";
			case EJournalEventKind::Interrupted:
				return "Interrupted";
			case EJournalEventKind::Aborted:
				return "Aborted";
			case EJournalEventKind::RecoveredAfterUncleanExit:
				return "RecoveredAfterUncleanExit";
			}
			return "Unknown";
		}

		void InsertJournalEvent(FSqliteConnection &Connection,
								const FJournalEvent &Event)
		{
			auto Statement = Connection.Prepare(
				"INSERT INTO journal_events "
				"(session_id, sequence, monotonic_ns, kind, payload_version, "
				"payload_blob) VALUES (?, ?, ?, ?, ?, ?);");
			Statement.BindText(1, Event.SessionId);
			Statement.BindInt64(2, static_cast<std::int64_t>(Event.Sequence));
			Statement.BindInt64(3, static_cast<std::int64_t>(Event.MonotonicNs));
			Statement.BindText(4, JournalEventKindToString(Event.Kind));
			Statement.BindInt64(5,
								static_cast<std::int64_t>(Event.PayloadVersion));
			Statement.BindBlob(6, Event.PayloadBlob);
			Statement.Step();
		}

		void InsertSampleChunk(FSqliteConnection &Connection,
							   const FSampleChunk &Chunk)
		{
			const std::string Payload = EncodeSamples(Chunk.Samples);
			const std::uint32_t Checksum = ComputeCrc32c(Payload);

			auto Statement = Connection.Prepare(
				"INSERT INTO sample_chunks "
				"(session_id, first_sequence, last_sequence, codec, crc32c, "
				"payload_blob) VALUES (?, ?, ?, 'raw-v1', ?, ?);");
			Statement.BindText(1, Chunk.SessionId);
			Statement.BindInt64(2, static_cast<std::int64_t>(Chunk.FirstSequence));
			Statement.BindInt64(3, static_cast<std::int64_t>(Chunk.LastSequence));
			Statement.BindInt64(4, static_cast<std::int64_t>(Checksum));
			Statement.BindBlob(5, Payload);
			Statement.Step();
		}
	} // namespace

	struct FLocalDataJournalWriter::FImpl
	{
		Private::FSqliteConnection Connection;
		bool TransactionStaged = false;

		explicit FImpl(const std::filesystem::path &DatabasePath)
			: Connection(DatabasePath)
		{
			Private::EnsureSchema(Connection);
		}

		void CommitStagedTransaction(const char *const Context)
		{
			if (!TransactionStaged)
			{
				throw Private::FSqliteError(
					std::string(Context) + " called without a staged transaction");
			}
			Connection.Commit();
			TransactionStaged = false;
		}
	};

	FLocalDataJournalWriter::FLocalDataJournalWriter(
		std::filesystem::path DatabasePath)
		: Impl(std::make_unique<FImpl>(DatabasePath))
	{
	}

	FLocalDataJournalWriter::~FLocalDataJournalWriter() = default;

	void FLocalDataJournalWriter::AppendChunk(const FSampleChunk &Chunk)
	{
		StageChunk(Chunk);
		CommitStagedChunk();
	}

	void
	FLocalDataJournalWriter::RecordJournalEvent(const FJournalEvent &Event)
	{
		Impl->Connection.BeginImmediate();
		InsertJournalEvent(Impl->Connection, Event);
		Impl->Connection.Commit();
	}

	void FLocalDataJournalWriter::StageChunk(const FSampleChunk &Chunk)
	{
		Impl->Connection.BeginImmediate();
		InsertSampleChunk(Impl->Connection, Chunk);
		Impl->TransactionStaged = true;
	}

	void FLocalDataJournalWriter::CommitStagedChunk()
	{
		Impl->CommitStagedTransaction("CommitStagedChunk");
	}

	void FLocalDataJournalWriter::StageFinalEvent(const FJournalEvent &Event)
	{
		Impl->Connection.BeginImmediate();
		InsertJournalEvent(Impl->Connection, Event);
		Impl->TransactionStaged = true;
	}

	void FLocalDataJournalWriter::CommitStagedFinalEvent()
	{
		Impl->CommitStagedTransaction("CommitStagedFinalEvent");
	}

	void FLocalDataJournalWriter::Close()
	{
		Impl.reset();
	}

	std::vector<FSampleChunk>
	ReadSampleChunks(const std::filesystem::path &DatabasePath,
					 const std::string &SessionId)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		std::vector<FSampleChunk> Chunks;
		auto Statement = Connection.Prepare(
			"SELECT first_sequence, last_sequence, payload_blob "
			"FROM sample_chunks WHERE session_id = ? ORDER BY first_sequence;");
		Statement.BindText(1, SessionId);
		while (Statement.Step())
		{
			FSampleChunk Chunk;
			Chunk.SessionId = SessionId;
			Chunk.FirstSequence =
				static_cast<std::uint64_t>(Statement.ColumnInt64(0));
			Chunk.LastSequence =
				static_cast<std::uint64_t>(Statement.ColumnInt64(1));
			Chunk.Samples = Private::DecodeSamples(Statement.ColumnBlob(2));
			Chunks.push_back(std::move(Chunk));
		}
		return Chunks;
	}
} // namespace LocalData
