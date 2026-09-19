#include "LocalData/LocalDataJournal.h"

#include "LocalData/Crc32c.h"
#include "LocalData/SampleChunkCodec.h"
#include "LocalData/Schema.h"
#include "LocalData/Sqlite.h"
#include "LocalData/TelemetryWireMapping.h"

#include "rowing/v1/session_object.pb.h"

#include <CommonCrypto/CommonDigest.h>
#include <google/protobuf/io/coded_stream.h>
#include <zstd.h>

#include <array>
#include <algorithm>
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

		std::string SessionDispositionName(EJournalEventKind Kind)
		{
			switch (Kind)
			{
			case EJournalEventKind::Completed:
				return "completed";
			case EJournalEventKind::Interrupted:
				return "interrupted";
			case EJournalEventKind::Aborted:
				return "aborted";
			default:
				throw Private::FSqliteError("SessionObject requires a terminal disposition");
			}
		}

		EJournalEventKind JournalEventKindFromString(const std::string &Kind)
		{
			if (Kind == "Started")
				return EJournalEventKind::Started;
			if (Kind == "Completed")
				return EJournalEventKind::Completed;
			if (Kind == "Interrupted")
				return EJournalEventKind::Interrupted;
			if (Kind == "Aborted")
				return EJournalEventKind::Aborted;
			if (Kind == "RecoveredAfterUncleanExit")
				return EJournalEventKind::RecoveredAfterUncleanExit;
			if (Kind == "CapabilityObserved")
				return EJournalEventKind::CapabilityObserved;
			if (Kind == "LinkGap")
				return EJournalEventKind::LinkGap;
			throw Private::FSqliteError("journal_events.kind has unknown value: " + Kind);
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
				"payload_blob, codec) VALUES (?, ?, ?, ?, ?, ?, ?);");
			Statement.BindText(1, Event.SessionId);
			Statement.BindInt64(2, static_cast<std::int64_t>(Event.Sequence));
			Statement.BindInt64(3, static_cast<std::int64_t>(Event.MonotonicNs));
			Statement.BindText(4, JournalEventKindToString(Event.Kind));
			Statement.BindInt64(5,
								static_cast<std::int64_t>(Event.PayloadVersion));
			Statement.BindBlob(6, Payload);
			Statement.BindText(7, Cipher != nullptr && !Event.PayloadBlob.empty() ? "raw-v1+sealed" : "raw-v1");
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

		std::string_view SessionIdBlob(const FRowingSessionId &Id);

		std::string Sha256Hex(std::string_view Bytes)
		{
			unsigned char Digest[CC_SHA256_DIGEST_LENGTH]{};
			CC_SHA256(Bytes.data(), static_cast<CC_LONG>(Bytes.size()), Digest);
			static constexpr char Hex[] = "0123456789abcdef";
			std::string Result;
			Result.reserve(CC_SHA256_DIGEST_LENGTH * 2);
			for (const unsigned char Byte : Digest)
			{
				Result.push_back(Hex[(Byte >> 4) & 0x0F]);
				Result.push_back(Hex[Byte & 0x0F]);
			}
			return Result;
		}

		void SerializeDeterministically(const rowing::v1::SessionObject &Object, std::string &Bytes)
		{
			Bytes.clear();
			Bytes.resize(Object.ByteSizeLong());
			google::protobuf::io::StringOutputStream Output(&Bytes);
			google::protobuf::io::CodedOutputStream Coded(&Output);
			Coded.SetSerializationDeterministic(true);
			if (!Object.SerializeToCodedStream(&Coded))
				throw Private::FSqliteError("SessionObject protobuf serialization failed");
		}

		std::string BuildSessionObjectBytes(FSqliteConnection &Connection,
											const FFinalizedSession &Finalized,
											IBlobCipher &Cipher)
		{
			rowing::v1::SessionObject Object;
			auto *Header = Object.mutable_header();
			Header->set_container_version(1);
			Header->set_schema_version(1);
			Header->set_session_id(SessionIdBlob(Finalized.Summary.Id).data(), FRowingSessionId::ByteLength);
			Header->set_disposition(SessionDispositionName(Finalized.TerminalEvent.Kind));
			Header->set_provenance("local_journal");
			Header->set_quality_flags(Finalized.Summary.QualityFlags);
			Header->set_compression("zstd");

			auto Events = Connection.Prepare("SELECT sequence, monotonic_ns, kind, payload_version, payload_blob, codec FROM journal_events WHERE session_id = ? ORDER BY sequence;");
			Events.BindText(1, Finalized.TerminalEvent.SessionId);
			bool bTerminalAlreadyStored = false;
			while (Events.Step())
			{
				auto *Event = Object.add_events();
				Event->set_sequence(static_cast<std::uint64_t>(Events.ColumnInt64(0)));
				Event->set_monotonic_ns(static_cast<std::uint64_t>(Events.ColumnInt64(1)));
				Event->set_kind(Events.ColumnText(2));
				Event->set_payload_version(static_cast<std::uint32_t>(Events.ColumnInt64(3)));
				const std::string Kind = Events.ColumnText(2);
				std::string Payload = Events.ColumnBlob(4);
				if (!Payload.empty() && Events.ColumnText(5) == "raw-v1+sealed")
					Payload = Cipher.Open(Payload, EventAssociatedData({Finalized.TerminalEvent.SessionId, Event->sequence(), Event->monotonic_ns(), JournalEventKindFromString(Kind), Event->payload_version(), {}}));
				Event->set_payload(Payload);
				bTerminalAlreadyStored = bTerminalAlreadyStored || (Event->sequence() == Finalized.TerminalEvent.Sequence && Kind == JournalEventKindToString(Finalized.TerminalEvent.Kind));
			}
			if (!bTerminalAlreadyStored)
			{
				auto *Terminal = Object.add_events();
				Terminal->set_sequence(Finalized.TerminalEvent.Sequence);
				Terminal->set_monotonic_ns(Finalized.TerminalEvent.MonotonicNs);
				Terminal->set_kind(JournalEventKindToString(Finalized.TerminalEvent.Kind));
				Terminal->set_payload_version(Finalized.TerminalEvent.PayloadVersion);
				Terminal->set_payload(Finalized.TerminalEvent.PayloadBlob);
			}

			auto Chunks = Connection.Prepare("SELECT first_sequence, last_sequence, codec, payload_blob, crc32c FROM sample_chunks WHERE session_id = ? ORDER BY first_sequence;");
			Chunks.BindText(1, Finalized.TerminalEvent.SessionId);
			while (Chunks.Step())
			{
				auto *Chunk = Object.add_chunks();
				Chunk->set_first_sequence(static_cast<std::uint64_t>(Chunks.ColumnInt64(0)));
				Chunk->set_last_sequence(static_cast<std::uint64_t>(Chunks.ColumnInt64(1)));
				std::string Payload = Chunks.ColumnBlob(3);
				if (Chunks.ColumnText(2) == SealedCodec)
					Payload = Cipher.Open(Payload, ChunkAssociatedData(Finalized.TerminalEvent.SessionId, Chunk->first_sequence(), Chunk->last_sequence()));
				Chunk->set_validated_payload(Payload);
				Chunk->set_crc32c(static_cast<std::uint32_t>(Chunks.ColumnInt64(4)));
			}
			auto *Footer = Object.mutable_footer();
			Footer->set_event_count(static_cast<std::uint64_t>(Object.events_size()));
			Footer->set_sealed_summary(Finalized.Summary.MetricsPayload);
			Footer->set_quality_flags(Finalized.Summary.QualityFlags);
			std::string Canonical;
			SerializeDeterministically(Object, Canonical);
			Footer->set_canonical_sha256(Sha256Hex(Canonical));
			SerializeDeterministically(Object, Canonical);
			const std::size_t Bound = ZSTD_compressBound(Canonical.size());
			std::string Compressed(Bound, '\0');
			const std::size_t Written = ZSTD_compress(Compressed.data(), Bound, Canonical.data(), Canonical.size(), 3);
			if (ZSTD_isError(Written))
				throw Private::FSqliteError(std::string("SessionObject zstd compression failed: ") + ZSTD_getErrorName(Written));
			Compressed.resize(Written);
			return Compressed;
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

	void FLocalDataJournalWriter::FinalizeSession(const FFinalizedSession &Finalized)
	{
		if (Impl->Cipher == nullptr)
			throw FBlobCipherError("FinalizeSession requires a cipher");
		if (Finalized.TerminalEvent.Kind != EJournalEventKind::Completed &&
			Finalized.TerminalEvent.Kind != EJournalEventKind::Interrupted &&
			Finalized.TerminalEvent.Kind != EJournalEventKind::Aborted)
			throw Private::FSqliteError("FinalizeSession requires a terminal event");
		if (Finalized.TerminalEvent.SessionId != Finalized.Summary.Id.ToCanonicalString())
			throw Private::FSqliteError("FinalizeSession session identity mismatch");
		const std::string OperationId = "session-sync-v1:" + Finalized.Summary.Id.ToCanonicalString();
		{
			auto Existing = Impl->Connection.Prepare("SELECT payload_hash FROM sync_outbox WHERE operation_id = ?;");
			Existing.BindText(1, OperationId);
			if (Existing.Step())
			{
				if (Finalized.ObjectDigestSha256.empty() || Existing.ColumnText(0) == Finalized.ObjectDigestSha256)
					return;
				throw Private::FSqliteError("FinalizeSession idempotency digest conflict");
			}
		}
		std::string ObjectDigest = Finalized.ObjectDigestSha256;
		if (ObjectDigest.empty())
			ObjectDigest = Sha256Hex(BuildSessionObjectBytes(Impl->Connection, Finalized, *Impl->Cipher));
		const bool bDigestIsLowercaseHex = ObjectDigest.size() == 64 &&
										   std::all_of(ObjectDigest.begin(), ObjectDigest.end(), [](unsigned char Character)
													   { return (Character >= '0' && Character <= '9') || (Character >= 'a' && Character <= 'f'); });
		if (!bDigestIsLowercaseHex)
			throw Private::FSqliteError("FinalizeSession requires a SHA-256 digest");

		Impl->InTransaction([&]
							{
			InsertJournalEvent(Impl->Connection, Finalized.TerminalEvent, Impl->Cipher);
			auto End = Impl->Connection.Prepare("UPDATE sessions SET state = 'Ended' WHERE session_id = ?;");
			End.BindBlob(1, SessionIdBlob(Finalized.Summary.Id));
			End.Step();
			if (Impl->Connection.ChangedRowCount() != 1)
				throw Private::FSqliteError("FinalizeSession: no such session");
			const std::string Sealed = Impl->Cipher->Seal(Finalized.Summary.MetricsPayload, SummaryAssociatedData(Finalized.Summary.Id, Finalized.Summary.Revision));
			auto Summary = Impl->Connection.Prepare("INSERT INTO session_summaries (session_id, revision, metrics_blob, quality_flags, finalized_at) VALUES (?, ?, ?, ?, datetime('now')); ");
			Summary.BindBlob(1, SessionIdBlob(Finalized.Summary.Id));
			Summary.BindInt64(2, static_cast<std::int64_t>(Finalized.Summary.Revision));
			Summary.BindBlob(3, Sealed);
			Summary.BindInt64(4, static_cast<std::int64_t>(Finalized.Summary.QualityFlags));
			Summary.Step();
			auto Outbox = Impl->Connection.Prepare("INSERT INTO sync_outbox (operation_id, aggregate_id, kind, attempt, next_attempt_at, payload_hash, state) VALUES (?, ?, 'session_object_v1', 0, datetime('now'), ?, 'queued');");
			Outbox.BindText(1, OperationId);
			Outbox.BindBlob(2, SessionIdBlob(Finalized.Summary.Id));
			Outbox.BindText(3, ObjectDigest);
			Outbox.Step(); });
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

	std::vector<FJournalEvent> ReadJournalEvents(const std::filesystem::path &DatabasePath, const std::string &SessionId, IBlobCipher *Cipher, bool bAllowLegacyPlaintext)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		auto Statement = Connection.Prepare("SELECT sequence, monotonic_ns, kind, payload_version, payload_blob, codec FROM journal_events WHERE session_id = ? ORDER BY sequence;");
		Statement.BindText(1, SessionId);
		std::vector<FJournalEvent> Events;
		while (Statement.Step())
		{
			FJournalEvent Event;
			Event.SessionId = SessionId;
			Event.Sequence = static_cast<std::uint64_t>(Statement.ColumnInt64(0));
			Event.MonotonicNs = static_cast<std::uint64_t>(Statement.ColumnInt64(1));
			Event.Kind = JournalEventKindFromString(Statement.ColumnText(2));
			Event.PayloadVersion = static_cast<std::uint32_t>(Statement.ColumnInt64(3));
			Event.PayloadBlob = Statement.ColumnBlob(4);
			const std::string Codec = Statement.ColumnText(5);
			if (Codec == "raw-v1+sealed")
			{
				if (Cipher == nullptr)
					throw FBlobCipherError("journal event is sealed but no cipher was supplied");
				if (!Event.PayloadBlob.empty())
					Event.PayloadBlob = Cipher->Open(Event.PayloadBlob, EventAssociatedData(Event));
			}
			else if (Codec == "raw-v1")
			{
				if (Cipher != nullptr && !bAllowLegacyPlaintext && !Event.PayloadBlob.empty())
					throw FBlobCipherError("plaintext journal event rejected because a cipher was supplied");
			}
			else
				throw Private::FSqliteError("journal_events.codec has unknown value: " + Codec);
			Events.push_back(std::move(Event));
		}
		return Events;
	}

	std::vector<FSyncOutboxItem> ReadPendingSyncOutbox(const std::filesystem::path &DatabasePath)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		auto Statement = Connection.Prepare("SELECT operation_id, aggregate_id, payload_hash, attempt, next_attempt_at, state, last_error FROM sync_outbox WHERE state IN ('queued', 'uploading', 'processing') AND (next_attempt_at IS NULL OR next_attempt_at <= datetime('now')) ORDER BY next_attempt_at, operation_id;");
		std::vector<FSyncOutboxItem> Items;
		while (Statement.Step())
		{
			FSyncOutboxItem Item;
			Item.OperationId = Statement.ColumnText(0);
			const std::string Id = Statement.ColumnBlob(1);
			if (Id.size() != FRowingSessionId::ByteLength)
				throw Private::FSqliteError("sync_outbox aggregate_id is not a session id");
			std::array<std::uint8_t, FRowingSessionId::ByteLength> Bytes{};
			std::copy(Id.begin(), Id.end(), reinterpret_cast<char *>(Bytes.data()));
			Item.SessionId = FRowingSessionId::FromBytes(Bytes);
			Item.ObjectDigestSha256 = Statement.ColumnText(2);
			Item.Attempt = static_cast<std::uint32_t>(Statement.ColumnInt64(3));
			Item.NextAttemptAtUtc = Statement.ColumnOptionalText(4);
			Item.State = Statement.ColumnText(5);
			Item.LastError = Statement.ColumnOptionalText(6);
			Items.push_back(std::move(Item));
		}
		return Items;
	}

	void UpdateSyncOutbox(const std::filesystem::path &DatabasePath,
						  const std::string &OperationId,
						  const std::string &State,
						  std::uint32_t Attempt,
						  const std::string &LastError)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		Connection.InTransaction([&]
								 {
			auto Statement = Connection.Prepare("UPDATE sync_outbox SET state = ?, attempt = ?, next_attempt_at = datetime('now', '+1 minute'), last_error = ? WHERE operation_id = ?;");
			Statement.BindText(1, State);
			Statement.BindInt64(2, static_cast<std::int64_t>(Attempt));
			if (LastError.empty()) Statement.BindNull(3); else Statement.BindText(3, LastError);
			Statement.BindText(4, OperationId);
			Statement.Step();
			if (Connection.ChangedRowCount() != 1)
				throw Private::FSqliteError("UpdateSyncOutbox: no such operation"); });
	}

	std::string ReadSessionObject(const std::filesystem::path &DatabasePath, const FRowingSessionId &Id, IBlobCipher &Cipher)
	{
		Private::FSqliteConnection Connection(DatabasePath);
		Private::EnsureSchema(Connection);
		const std::string SessionId = Id.ToCanonicalString();
		auto EventQuery = Connection.Prepare("SELECT sequence, monotonic_ns, kind, payload_version, payload_blob, codec FROM journal_events WHERE session_id = ? ORDER BY sequence DESC LIMIT 1;");
		EventQuery.BindText(1, SessionId);
		if (!EventQuery.Step())
			throw Private::FSqliteError("ReadSessionObject: no journal events");
		FJournalEvent Terminal;
		Terminal.SessionId = SessionId;
		Terminal.Sequence = static_cast<std::uint64_t>(EventQuery.ColumnInt64(0));
		Terminal.MonotonicNs = static_cast<std::uint64_t>(EventQuery.ColumnInt64(1));
		Terminal.Kind = JournalEventKindFromString(EventQuery.ColumnText(2));
		Terminal.PayloadVersion = static_cast<std::uint32_t>(EventQuery.ColumnInt64(3));
		Terminal.PayloadBlob = EventQuery.ColumnBlob(4);
		if (!Terminal.PayloadBlob.empty() && EventQuery.ColumnText(5) == "raw-v1+sealed")
			Terminal.PayloadBlob = Cipher.Open(Terminal.PayloadBlob, EventAssociatedData(Terminal));
		if (Terminal.Kind != EJournalEventKind::Completed && Terminal.Kind != EJournalEventKind::Interrupted && Terminal.Kind != EJournalEventKind::Aborted)
			throw Private::FSqliteError("ReadSessionObject: session is not finalized");
		const auto Summary = ReadLatestSessionSummary(DatabasePath, Id, Cipher);
		if (!Summary)
			throw Private::FSqliteError("ReadSessionObject: session has no summary");
		FFinalizedSession Finalized;
		Finalized.TerminalEvent = std::move(Terminal);
		Finalized.Summary = *Summary;
		return BuildSessionObjectBytes(Connection, Finalized, Cipher);
	}
} // namespace LocalData
