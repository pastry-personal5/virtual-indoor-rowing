#include "LocalData/LocalDataJournal.h"

#include "LocalData/Crc32c.h"
#include "LocalData/SampleChunkCodec.h"
#include "LocalData/Schema.h"
#include "LocalData/Sqlite.h"
#include "LocalData/TelemetryWireMapping.h"

#include <array>
#include <utility>

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
			case EJournalEventKind::CapabilityObserved:
				return "CapabilityObserved";
			case EJournalEventKind::LinkGap:
				return "LinkGap";
			}
			return "Unknown";
		}

		std::string EventAssociatedData(const FJournalEvent &Event)
		{
			return "event|" + Event.SessionId + "|" + std::to_string(Event.Sequence) + "|" + JournalEventKindToString(Event.Kind);
		}

		// With a cipher, a non-empty payload is sealed like chunks and summaries;
		// an empty payload carries nothing to protect and stays empty.
		void InsertJournalEvent(FSqliteConnection &Connection,
								const FJournalEvent &Event,
								IBlobCipher *Cipher)
		{
			const std::string Payload = Cipher != nullptr && !Event.PayloadBlob.empty()
											? Cipher->Seal(Event.PayloadBlob, EventAssociatedData(Event))
											: Event.PayloadBlob;
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
			Statement.BindBlob(6, Payload);
			Statement.Step();
		}

		constexpr const char *PlainCodec = "raw-v1";
		constexpr const char *SealedCodec = "raw-v1+sealed";

		std::string ChunkAssociatedData(const std::string &SessionId, std::uint64_t First, std::uint64_t Last)
		{
			return "chunk|" + SessionId + "|" + std::to_string(First) + "|" + std::to_string(Last);
		}

		std::string SummaryAssociatedData(const FRowingSessionId &Id, std::uint32_t Revision)
		{
			return "summary|" + Id.ToCanonicalString() + "|" + std::to_string(Revision);
		}

		std::string_view SessionIdBlob(const FRowingSessionId &Id)
		{
			return std::string_view(reinterpret_cast<const char *>(Id.GetBytes().data()), FRowingSessionId::ByteLength);
		}

		constexpr std::array<std::pair<ERowingSessionState, std::string_view>, 4> SessionStateNames{{
			{ERowingSessionState::Created, "Created"},
			{ERowingSessionState::Active, "Active"},
			{ERowingSessionState::ConnectionLost, "ConnectionLost"},
			{ERowingSessionState::Ended, "Ended"},
		}};

		std::string_view SessionStateToString(ERowingSessionState State)
		{
			for (const auto &[Value, Name] : SessionStateNames)
			{
				if (Value == State)
					return Name;
			}
			throw Private::FSqliteError("unknown session state");
		}

		ERowingSessionState SessionStateFromString(const std::string &Name)
		{
			for (const auto &[Value, Candidate] : SessionStateNames)
			{
				if (Candidate == Name)
					return Value;
			}
			throw Private::FSqliteError("sessions.state has unknown value: " + Name);
		}

		void InsertSampleChunk(FSqliteConnection &Connection,
							   const FSampleChunk &Chunk,
							   IBlobCipher *Cipher)
		{
			std::string Payload = EncodeSamples(Chunk.Samples);
			if (Cipher != nullptr)
			{
				Payload = Cipher->Seal(Payload, ChunkAssociatedData(Chunk.SessionId, Chunk.FirstSequence, Chunk.LastSequence));
			}
			// The checksum covers the stored bytes, so recovery can validate a
			// sealed chunk without holding the key.
			const std::uint32_t Checksum = ComputeCrc32c(Payload);

			auto Statement = Connection.Prepare(
				"INSERT INTO sample_chunks "
				"(session_id, first_sequence, last_sequence, codec, crc32c, "
				"payload_blob) VALUES (?, ?, ?, ?, ?, ?);");
			Statement.BindText(1, Chunk.SessionId);
			Statement.BindInt64(2, static_cast<std::int64_t>(Chunk.FirstSequence));
			Statement.BindInt64(3, static_cast<std::int64_t>(Chunk.LastSequence));
			Statement.BindText(4, Cipher != nullptr ? SealedCodec : PlainCodec);
			Statement.BindInt64(5, static_cast<std::int64_t>(Checksum));
			Statement.BindBlob(6, Payload);
			Statement.Step();
		}
	} // namespace

	struct FLocalDataJournalWriter::FImpl
	{
		Private::FSqliteConnection Connection;
		IBlobCipher *Cipher = nullptr;
		bool TransactionStaged = false;

		FImpl(const std::filesystem::path &DatabasePath, IBlobCipher *InCipher)
			: Connection(DatabasePath), Cipher(InCipher)
		{
			Private::EnsureSchema(Connection);
		}

		template <typename FBody>
		void InTransaction(FBody &&Body)
		{
			Connection.InTransaction(std::forward<FBody>(Body));
		}

		template <typename FBody>
		void StageInTransaction(FBody &&Body)
		{
			Connection.BeginImmediate();
			try
			{
				Body();
			}
			catch (...)
			{
				Connection.RollbackNoThrow();
				throw;
			}
			TransactionStaged = true;
		}

		// A failed COMMIT (e.g. SQLITE_BUSY) leaves the transaction open with the
		// staged rows intact, so the caller may retry. The stage is only forgotten
		// once SQLite has itself discarded the transaction.
		void CommitStagedTransaction(const char *const Context)
		{
			if (!TransactionStaged)
			{
				throw Private::FSqliteError(
					std::string(Context) + " called without a staged transaction");
			}
			try
			{
				Connection.Commit();
				TransactionStaged = false;
			}
			catch (...)
			{
				if (!Connection.IsInTransaction())
				{
					TransactionStaged = false;
				}
				throw;
			}
		}
	};

	FLocalDataJournalWriter::FLocalDataJournalWriter(
		std::filesystem::path DatabasePath, IBlobCipher *Cipher)
		: Impl(std::make_unique<FImpl>(DatabasePath, Cipher))
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
		Impl->InTransaction([&]
							{ InsertJournalEvent(Impl->Connection, Event, Impl->Cipher); });
	}

	void FLocalDataJournalWriter::RecordCapabilityObserved(const std::string &SessionId,
														   std::uint64_t Sequence,
														   std::uint64_t MonotonicNs,
														   const FRowingMachineInfo &Info)
	{
		FJournalEvent Event;
		Event.SessionId = SessionId;
		Event.Sequence = Sequence;
		Event.MonotonicNs = MonotonicNs;
		Event.Kind = EJournalEventKind::CapabilityObserved;
		Event.PayloadVersion = LocalData::Private::TelemetryContractVersion;
		Event.PayloadBlob = LocalData::Private::SerializeMachineInfo(Info);
		RecordJournalEvent(Event);
	}

	void FLocalDataJournalWriter::StageChunk(const FSampleChunk &Chunk)
	{
		Impl->StageInTransaction([&]
								 { InsertSampleChunk(Impl->Connection, Chunk, Impl->Cipher); });
	}

	void FLocalDataJournalWriter::CommitStagedChunk()
	{
		Impl->CommitStagedTransaction("CommitStagedChunk");
	}

	void FLocalDataJournalWriter::StageFinalEvent(const FJournalEvent &Event)
	{
		Impl->StageInTransaction([&]
								 { InsertJournalEvent(Impl->Connection, Event, Impl->Cipher); });
	}

	void FLocalDataJournalWriter::CommitStagedFinalEvent()
	{
		Impl->CommitStagedTransaction("CommitStagedFinalEvent");
	}

	void FLocalDataJournalWriter::CreateSession(const FSessionRecord &Session)
	{
		Impl->InTransaction([&]
							{
			auto Statement = Impl->Connection.Prepare(
				"INSERT INTO sessions (session_id, user_scope, plan_id, route_id, state, "
				"started_at_utc, timezone, source, content_hash, created_at) "
				"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now'));");
			Statement.BindBlob(1, SessionIdBlob(Session.Id));
			Statement.BindText(2, Session.UserScope);
			Statement.BindOptionalText(3, Session.PlanId);
			Statement.BindOptionalText(4, Session.RouteId);
			Statement.BindText(5, SessionStateToString(Session.State));
			Statement.BindOptionalText(6, Session.StartedAtUtc);
			Statement.BindOptionalText(7, Session.Timezone);
			Statement.BindText(8, Session.Source);
			Statement.BindOptionalText(9, Session.ContentHash);
			Statement.Step(); });
	}

	void FLocalDataJournalWriter::UpdateSessionState(const FRowingSessionId &Id, ERowingSessionState State)
	{
		Impl->InTransaction([&]
							{
			auto Current = Impl->Connection.Prepare("SELECT state FROM sessions WHERE session_id = ?;");
				Current.BindBlob(1, SessionIdBlob(Id));
				if (Current.Step() && State != ERowingSessionState::Ended && SessionStateFromString(Current.ColumnText(0)) == ERowingSessionState::Ended)
				{
					throw Private::FSqliteError("UpdateSessionState: Ended is terminal");
				}
			auto Statement = Impl->Connection.Prepare("UPDATE sessions SET state = ? WHERE session_id = ?;");
			Statement.BindText(1, SessionStateToString(State));
			Statement.BindBlob(2, SessionIdBlob(Id));
			Statement.Step();
			if (Impl->Connection.ChangedRowCount() != 1)
			{
				throw Private::FSqliteError("UpdateSessionState: no such session");
			} });
	}

	void FLocalDataJournalWriter::StageSessionSummary(const FSessionSummary &Summary)
	{
		if (Impl->Cipher == nullptr)
		{
			throw FBlobCipherError("StageSessionSummary requires a cipher");
		}
		Impl->StageInTransaction([&]
								 {
			const std::string Sealed = Impl->Cipher->Seal(Summary.MetricsPayload, SummaryAssociatedData(Summary.Id, Summary.Revision));
			auto Statement = Impl->Connection.Prepare(
				"INSERT INTO session_summaries (session_id, revision, metrics_blob, quality_flags, finalized_at) "
				"VALUES (?, ?, ?, ?, datetime('now'));");
			Statement.BindBlob(1, SessionIdBlob(Summary.Id));
			Statement.BindInt64(2, static_cast<std::int64_t>(Summary.Revision));
			Statement.BindBlob(3, Sealed);
			Statement.BindInt64(4, static_cast<std::int64_t>(Summary.QualityFlags));
			Statement.Step(); });
	}

	void FLocalDataJournalWriter::CommitStagedSessionSummary()
	{
		Impl->CommitStagedTransaction("CommitStagedSessionSummary");
	}

	void FLocalDataJournalWriter::AbandonStaged() noexcept
	{
		if (!Impl->TransactionStaged)
			return;
		Impl->Connection.RollbackNoThrow();
		Impl->TransactionStaged = false;
	}

	void FLocalDataJournalWriter::Close()
	{
		Impl.reset();
	}

	std::vector<FSampleChunk>
	ReadSampleChunks(const std::filesystem::path &DatabasePath,
					 const std::string &SessionId,
					 IBlobCipher *Cipher,
					 bool bAllowLegacyPlaintext)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		std::vector<FSampleChunk> Chunks;
		auto Statement = Connection.Prepare(
			"SELECT first_sequence, last_sequence, codec, payload_blob "
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
			std::string Payload = Statement.ColumnBlob(3);
			const std::string Codec = Statement.ColumnText(2);
			if (Codec == SealedCodec)
			{
				if (Cipher == nullptr)
				{
					throw FBlobCipherError("sample chunk is sealed but no cipher was supplied");
				}
				Payload = Cipher->Open(Payload, ChunkAssociatedData(SessionId, Chunk.FirstSequence, Chunk.LastSequence));
			}
			else if (Codec == PlainCodec)
			{
				// The codec column is unauthenticated, so a reader holding a key
				// must not let a row downgrade itself to plaintext.
				if (Cipher != nullptr && !bAllowLegacyPlaintext)
				{
					throw FBlobCipherError("plaintext sample chunk rejected because a cipher was supplied");
				}
			}
			else
			{
				throw Private::FSqliteError("sample_chunks.codec has unknown value: " + Codec);
			}
			Chunk.Samples = Private::DecodeSamples(Payload);
			Chunks.push_back(std::move(Chunk));
		}
		return Chunks;
	}

	std::optional<FSessionRecord>
	ReadSession(const std::filesystem::path &DatabasePath, const FRowingSessionId &Id)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		auto Statement = Connection.Prepare(
			"SELECT user_scope, plan_id, route_id, state, started_at_utc, timezone, source, content_hash "
			"FROM sessions WHERE session_id = ?;");
		Statement.BindBlob(1, SessionIdBlob(Id));
		if (!Statement.Step())
		{
			return std::nullopt;
		}
		FSessionRecord Session;
		Session.Id = Id;
		Session.UserScope = Statement.ColumnText(0);
		Session.PlanId = Statement.ColumnOptionalText(1);
		Session.RouteId = Statement.ColumnOptionalText(2);
		Session.State = SessionStateFromString(Statement.ColumnText(3));
		Session.StartedAtUtc = Statement.ColumnOptionalText(4);
		Session.Timezone = Statement.ColumnOptionalText(5);
		Session.Source = Statement.ColumnText(6);
		Session.ContentHash = Statement.ColumnOptionalText(7);
		return Session;
	}

	std::optional<FSessionSummary>
	ReadLatestSessionSummary(const std::filesystem::path &DatabasePath, const FRowingSessionId &Id, IBlobCipher &Cipher)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		auto Statement = Connection.Prepare(
			"SELECT revision, metrics_blob, quality_flags FROM session_summaries "
			"WHERE session_id = ? ORDER BY revision DESC LIMIT 1;");
		Statement.BindBlob(1, SessionIdBlob(Id));
		if (!Statement.Step())
		{
			return std::nullopt;
		}
		FSessionSummary Summary;
		Summary.Id = Id;
		Summary.Revision = static_cast<std::uint32_t>(Statement.ColumnInt64(0));
		Summary.MetricsPayload = Cipher.Open(Statement.ColumnBlob(1), SummaryAssociatedData(Id, Summary.Revision));
		Summary.QualityFlags = static_cast<std::uint32_t>(Statement.ColumnInt64(2));
		return Summary;
	}
} // namespace LocalData
