#include "LocalData/LocalDataJournal.h"

#include "LocalData/Crc32c.h"
#include "LocalData/SampleChunkCodec.h"
#include "LocalData/Sqlite.h"

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace
{
	int Failures = 0;

	void Expect(const bool Condition, const char *const Expression, const char *const TestName)
	{
		if (!Condition)
		{
			std::cerr << TestName << ": expectation failed: " << Expression
					  << '\n';
			++Failures;
		}
	}

#define EXPECT_TRUE(Expression) Expect((Expression), #Expression, __func__)

	void RemoveDatabase(const std::filesystem::path &Path)
	{
		std::error_code Ignored;
		std::filesystem::remove(Path, Ignored);
		std::filesystem::remove(Path.string() + "-wal", Ignored);
		std::filesystem::remove(Path.string() + "-shm", Ignored);
	}

	std::filesystem::path MakeTempDatabasePath(const char *const TestName)
	{
		std::random_device Random;
		const auto Path = std::filesystem::temp_directory_path() /
						  (std::string("local_data_tests_") + TestName +
						   "_" + std::to_string(Random()) + ".sqlite3");
		RemoveDatabase(Path);
		return Path;
	}

	FRowingMetricSample MakeSample(std::uint64_t Sequence)
	{
		FRowingMetricSample Sample;
		Sample.Sequence = Sequence;
		Sample.SourceElapsedMs = Sequence * 100;
		Sample.ReceivedMonotonicNs = Sequence * 100'000'000ULL;
		Sample.DistanceMm = Sequence * 1'700;
		Sample.SpeedMmPerS = 4'200;
		Sample.StrokeRateDeciSpm = 240;
		Sample.WorkoutState = ERowingWorkoutState::Active;
		Sample.RowingState = ERowingState::Active;
		Sample.StrokeState = ERowingStrokeState::Drive;
		return Sample;
	}

	LocalData::FSampleChunk MakeChunk(const std::string &SessionId,
									  std::uint64_t FirstSequence,
									  std::uint64_t Count)
	{
		LocalData::FSampleChunk Chunk;
		Chunk.SessionId = SessionId;
		Chunk.FirstSequence = FirstSequence;
		Chunk.LastSequence = FirstSequence + Count - 1;
		for (std::uint64_t Index = 0; Index < Count; ++Index)
		{
			Chunk.Samples.push_back(MakeSample(FirstSequence + Index));
		}
		return Chunk;
	}

	void schema_bootstrap_is_idempotent()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
		}
		{
			// Reopening an already-migrated file must not throw or
			// duplicate the schema_migrations row.
			LocalData::FLocalDataJournalWriter Writer(Path);
		}
		RemoveDatabase(Path);
	}

	std::int64_t CountRows(LocalData::Private::FSqliteConnection &Connection, const char *const Sql)
	{
		auto Statement = Connection.Prepare(Sql);
		Statement.Step();
		return Statement.ColumnInt64(0);
	}

	void local_data_schema_migrates_v1_journal_without_data_loss()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			// Hand-build a Spike B v1 database: only schema_migrations v1,
			// journal_events, and sample_chunks, with one row in each.
			LocalData::Private::FSqliteConnection Connection(Path);
			Connection.Execute("CREATE TABLE schema_migrations ("
							   "version INTEGER PRIMARY KEY, checksum TEXT NOT NULL, applied_at TEXT NOT NULL);");
			Connection.Execute("CREATE TABLE journal_events ("
							   "session_id TEXT NOT NULL, sequence INTEGER NOT NULL, monotonic_ns INTEGER NOT NULL,"
							   "kind TEXT NOT NULL, payload_version INTEGER NOT NULL, payload_blob BLOB,"
							   "PRIMARY KEY (session_id, sequence));");
			Connection.Execute("CREATE TABLE sample_chunks ("
							   "session_id TEXT NOT NULL, first_sequence INTEGER NOT NULL, last_sequence INTEGER NOT NULL,"
							   "codec TEXT NOT NULL, crc32c INTEGER NOT NULL, payload_blob BLOB NOT NULL,"
							   "PRIMARY KEY (session_id, first_sequence));");
			Connection.Execute("INSERT INTO schema_migrations VALUES (1, 'v1-journal_events-sample_chunks', datetime('now'));");
			Connection.Execute("INSERT INTO journal_events VALUES ('legacy', 0, 0, 'Started', 1, NULL);");
			Connection.Execute("INSERT INTO sample_chunks VALUES ('legacy', 0, 0, 'raw-v1', 0, x'00');");
		}

		{
			LocalData::FLocalDataJournalWriter Writer(Path);
		}

		LocalData::Private::FSqliteConnection Connection(Path);
		EXPECT_TRUE(CountRows(Connection, "SELECT COUNT(*) FROM journal_events WHERE session_id = 'legacy';") == 1);
		EXPECT_TRUE(CountRows(Connection, "SELECT COUNT(*) FROM sample_chunks WHERE session_id = 'legacy';") == 1);
		EXPECT_TRUE(CountRows(Connection, "SELECT COUNT(*) FROM schema_migrations;") == 7);
		for (const char *const Table : {"sessions", "session_summaries", "session_latency_summaries", "sync_outbox", "cloud_links", "installed_content", "paired_devices", "content_catalog_state", "content_downloads"})
		{
			const std::string Sql = std::string("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = '") + Table + "';";
			EXPECT_TRUE(CountRows(Connection, Sql.c_str()) == 1);
		}

		RemoveDatabase(Path);
	}

	// Deterministic stand-in for the CryptoKit adapter: obscures bytes and
	// authenticates associated data, so LocalData-level behavior is testable
	// without Keychain access. Not a real cipher.
	class FFakeCipher final : public LocalData::IBlobCipher
	{
	  public:
		bool bFailNextSeal = false;

		std::string Seal(std::string_view Plaintext, std::string_view AssociatedData) override
		{
			if (bFailNextSeal)
			{
				bFailNextSeal = false;
				throw LocalData::FBlobCipherError("injected seal failure");
			}
			std::string Out = Tag(AssociatedData);
			for (const char Byte : Plaintext)
				Out.push_back(static_cast<char>(Byte ^ 0x5A));
			return Out;
		}

		std::string Open(std::string_view Sealed, std::string_view AssociatedData) override
		{
			const std::string Expected = Tag(AssociatedData);
			if (Sealed.size() < Expected.size() || Sealed.substr(0, Expected.size()) != Expected)
			{
				throw LocalData::FBlobCipherError("authentication failed");
			}
			std::string Out;
			for (const char Byte : Sealed.substr(Expected.size()))
				Out.push_back(static_cast<char>(Byte ^ 0x5A));
			return Out;
		}

	  private:
		static std::string Tag(std::string_view AssociatedData)
		{
			const std::size_t Hash = std::hash<std::string_view>{}(AssociatedData);
			return std::string(reinterpret_cast<const char *>(&Hash), sizeof(Hash));
		}
	};

	template <typename FCallable>
	bool ThrowsAny(FCallable &&Callable)
	{
		try
		{
			Callable();
		}
		catch (const std::exception &)
		{
			return true;
		}
		return false;
	}

	std::string RawChunkBlob(const std::filesystem::path &Path, const char *const SessionId, std::int64_t FirstSequence)
	{
		LocalData::Private::FSqliteConnection Connection(Path);
		auto Statement = Connection.Prepare("SELECT payload_blob FROM sample_chunks WHERE session_id = ? AND first_sequence = ?;");
		Statement.BindText(1, SessionId);
		Statement.BindInt64(2, FirstSequence);
		Statement.Step();
		return Statement.ColumnBlob(0);
	}

	FRowingSessionId MakeSessionId(std::uint8_t Seed)
	{
		std::uint8_t Next = Seed;
		return FRowingSessionId::GenerateV7(1'700'000'000'000ULL, [&Next]
											{ return Next++; });
	}

	LocalData::FSessionRecord MakeSessionRecord(const FRowingSessionId &Id)
	{
		LocalData::FSessionRecord Record;
		Record.Id = Id;
		Record.UserScope = "guest";
		Record.Source = "pm5";
		return Record;
	}

	void finalized_session_commits_terminal_summary_and_outbox_atomically()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		const FRowingSessionId Id = MakeSessionId(10);
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			Writer.CreateSession(MakeSessionRecord(Id));
			LocalData::FFinalizedSession Finalized;
			Finalized.TerminalEvent = {Id.ToCanonicalString(), 99, 1234, LocalData::EJournalEventKind::Completed, 1, "terminal"};
			Finalized.Summary = {Id, 1, "summary", 7};
			Finalized.ObjectDigestSha256.clear();
			Writer.FinalizeSession(Finalized);
			// A restart/retry of the exact immutable object converges without
			// duplicating terminal evidence or its outbox operation.
			Writer.FinalizeSession(Finalized);
		}
		EXPECT_TRUE(LocalData::ReadSession(Path, Id)->State == ERowingSessionState::Ended);
		EXPECT_TRUE(LocalData::ReadLatestSessionSummary(Path, Id, Cipher)->MetricsPayload == "summary");
		const auto Events = LocalData::ReadJournalEvents(Path, Id.ToCanonicalString(), &Cipher);
		EXPECT_TRUE(Events.size() == 1 && Events[0].PayloadBlob == "terminal");
		const auto Outbox = LocalData::ReadPendingSyncOutbox(Path);
		EXPECT_TRUE(Outbox.size() == 1 && Outbox[0].SessionId == Id && Outbox[0].ObjectDigestSha256.size() == 64);
		const std::string Object = LocalData::ReadSessionObject(Path, Id, Cipher);
		EXPECT_TRUE(Object.size() > 4 && static_cast<unsigned char>(Object[0]) == 0x28 && static_cast<unsigned char>(Object[1]) == 0xB5 && static_cast<unsigned char>(Object[2]) == 0x2F && static_cast<unsigned char>(Object[3]) == 0xFD);

		const FRowingSessionId Missing = MakeSessionId(11);
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			LocalData::FFinalizedSession Bad;
			Bad.TerminalEvent = {Missing.ToCanonicalString(), 1, 1, LocalData::EJournalEventKind::Aborted, 1, "x"};
			Bad.Summary = {Missing, 1, "x", 0};
			Bad.ObjectDigestSha256 = std::string(64, 'b');
			EXPECT_TRUE(ThrowsAny([&]
								  { Writer.FinalizeSession(Bad); }));
		}
		EXPECT_TRUE(LocalData::ReadPendingSyncOutbox(Path).size() == 1);
		RemoveDatabase(Path);
	}

	void plaintext_finalized_session_round_trips_and_rejects_unknown_summary_codec()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		const FRowingSessionId Id = MakeSessionId(12);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			Writer.CreateSession(MakeSessionRecord(Id));
			LocalData::FFinalizedSession Finalized;
			Finalized.TerminalEvent = {Id.ToCanonicalString(), 1, 1234, LocalData::EJournalEventKind::Completed, 1, {}};
			Finalized.Summary = {Id, 1, "plaintext-summary", 3};
			Writer.FinalizeSession(Finalized);
		}
		const auto Summary = LocalData::ReadLatestSessionSummary(Path, Id);
		EXPECT_TRUE(Summary.has_value() && Summary->MetricsPayload == "plaintext-summary");
		EXPECT_TRUE(!LocalData::ReadSessionObject(Path, Id).empty());
		{
			LocalData::Private::FSqliteConnection Connection(Path);
			Connection.Execute("UPDATE session_summaries SET codec = 'mystery-v9';");
		}
		EXPECT_TRUE(ThrowsAny([&]
							  { LocalData::ReadLatestSessionSummary(Path, Id); }));
		EXPECT_TRUE(ThrowsAny([&]
							  { LocalData::ReadSessionObject(Path, Id); }));
		RemoveDatabase(Path);
	}

	void local_data_sample_chunk_payload_is_encrypted_at_rest()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		const auto Chunk = MakeChunk("session-g", 0, 10);
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			Writer.AppendChunk(Chunk);
		}

		const std::string Plain = LocalData::Private::EncodeSamples(Chunk.Samples);
		EXPECT_TRUE(RawChunkBlob(Path, "session-g", 0).find(Plain) == std::string::npos);

		const auto Chunks = LocalData::ReadSampleChunks(Path, "session-g", &Cipher);
		EXPECT_TRUE(Chunks.size() == 1);
		EXPECT_TRUE(Chunks[0].Samples.size() == 10);
		EXPECT_TRUE(Chunks[0].Samples[3].DistanceMm == 3 * 1'700);

		EXPECT_TRUE(ThrowsAny([&]
							  { LocalData::ReadSampleChunks(Path, "session-g"); }));
		RemoveDatabase(Path);
	}

	void local_data_sealed_chunk_is_bound_to_its_identity()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			Writer.AppendChunk(MakeChunk("session-h", 0, 10));
			Writer.AppendChunk(MakeChunk("session-h", 10, 10));
		}
		{
			// Replay chunk 0's ciphertext into chunk 10's row; ReadSampleChunks does
			// not consult the CRC, so only the associated-data check can reject it.
			const std::string Stolen = RawChunkBlob(Path, "session-h", 0);
			LocalData::Private::FSqliteConnection Connection(Path);
			auto Statement = Connection.Prepare("UPDATE sample_chunks SET payload_blob = ? WHERE session_id = 'session-h' AND first_sequence = 10;");
			Statement.BindBlob(1, Stolen);
			Statement.Step();
		}
		EXPECT_TRUE(ThrowsAny([&]
							  { LocalData::ReadSampleChunks(Path, "session-h", &Cipher); }));
		RemoveDatabase(Path);
	}

	void local_data_recovery_truncates_corrupt_sealed_chunk_without_a_key()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			Writer.AppendChunk(MakeChunk("session-i", 0, 10));
			Writer.AppendChunk(MakeChunk("session-i", 10, 10));
		}
		{
			LocalData::Private::FSqliteConnection Connection(Path);
			Connection.Execute("UPDATE sample_chunks SET payload_blob = x'DEADBEEF' WHERE session_id = 'session-i' AND first_sequence = 10;");
		}
		const auto Report = LocalData::ScanAndRecover(Path);
		EXPECT_TRUE(Report.TruncatedChunkCount == 1);
		EXPECT_TRUE(Report.HighestVerifiedSequence == 9);
		EXPECT_TRUE(LocalData::ReadSampleChunks(Path, "session-i", &Cipher).size() == 1);
		RemoveDatabase(Path);
	}

	void local_data_session_create_update_and_read_round_trips()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		const FRowingSessionId Id = MakeSessionId(1);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			LocalData::FSessionRecord Record;
			Record.Id = Id;
			Record.UserScope = "guest";
			Record.RouteId = "graybox-1";
			Record.Source = "pm5";
			Writer.CreateSession(Record);
			EXPECT_TRUE(ThrowsAny([&]
								  { Writer.CreateSession(Record); }));
			Writer.UpdateSessionState(Id, ERowingSessionState::Active);
			EXPECT_TRUE(ThrowsAny([&]
								  { Writer.UpdateSessionState(MakeSessionId(9), ERowingSessionState::Active); }));
		}

		const auto Read = LocalData::ReadSession(Path, Id);
		EXPECT_TRUE(Read.has_value());
		EXPECT_TRUE(Read->Id == Id);
		EXPECT_TRUE(Read->UserScope == "guest");
		EXPECT_TRUE(Read->State == ERowingSessionState::Active);
		EXPECT_TRUE(Read->RouteId == std::optional<std::string>("graybox-1"));
		EXPECT_TRUE(!Read->PlanId.has_value());
		EXPECT_TRUE(!LocalData::ReadSession(Path, MakeSessionId(9)).has_value());
		RemoveDatabase(Path);
	}

	void local_data_session_summary_is_sealed_and_commits_in_two_phases()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		const FRowingSessionId Id = MakeSessionId(2);
		const std::string Secret = "distance=5000123;avg_hr=150";
		{
			LocalData::FLocalDataJournalWriter NoCipherWriter(Path);
			LocalData::FSessionSummary Summary;
			Summary.Id = Id;
			EXPECT_TRUE(ThrowsAny([&]
								  { NoCipherWriter.StageSessionSummary(Summary); }));
		}
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			Writer.CreateSession(MakeSessionRecord(Id));
			LocalData::FSessionSummary Summary;
			Summary.Id = Id;
			Summary.Revision = 1;
			Summary.MetricsPayload = Secret;
			Summary.QualityFlags = 3;
			Writer.StageSessionSummary(Summary);
			// Destroyed without commit: must not survive.
		}
		EXPECT_TRUE(!LocalData::ReadLatestSessionSummary(Path, Id, Cipher).has_value());

		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			LocalData::FSessionSummary Summary;
			Summary.Id = Id;
			Summary.Revision = 1;
			Summary.MetricsPayload = Secret;
			Summary.QualityFlags = 3;
			Writer.StageSessionSummary(Summary);
			Writer.CommitStagedSessionSummary();
			Summary.Revision = 2;
			Summary.MetricsPayload = "revised";
			Writer.StageSessionSummary(Summary);
			Writer.CommitStagedSessionSummary();
		}

		const auto Latest = LocalData::ReadLatestSessionSummary(Path, Id, Cipher);
		EXPECT_TRUE(Latest.has_value());
		EXPECT_TRUE(Latest->Revision == 2);
		EXPECT_TRUE(Latest->MetricsPayload == "revised");
		EXPECT_TRUE(Latest->QualityFlags == 3);

		LocalData::Private::FSqliteConnection Connection(Path);
		auto Statement = Connection.Prepare("SELECT metrics_blob FROM session_summaries WHERE revision = 1;");
		Statement.Step();
		EXPECT_TRUE(Statement.ColumnBlob(0).find("distance=") == std::string::npos);
		RemoveDatabase(Path);
	}

	void append_and_read_back_round_trip()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			Writer.AppendChunk(MakeChunk("session-a", 0, 10));
			Writer.AppendChunk(MakeChunk("session-a", 10, 10));
		}

		const auto Chunks = LocalData::ReadSampleChunks(Path, "session-a");
		EXPECT_TRUE(Chunks.size() == 2);
		EXPECT_TRUE(Chunks[0].FirstSequence == 0);
		EXPECT_TRUE(Chunks[0].LastSequence == 9);
		EXPECT_TRUE(Chunks[0].Samples.size() == 10);
		EXPECT_TRUE(Chunks[0].Samples[3].Sequence == 3);
		EXPECT_TRUE(Chunks[0].Samples[3].DistanceMm == 3 * 1'700);
		EXPECT_TRUE(Chunks[0].Samples[3].SpeedMmPerS.has_value());
		EXPECT_TRUE(*Chunks[0].Samples[3].SpeedMmPerS == 4'200);
		EXPECT_TRUE(!Chunks[0].Samples[3].HeartRateBpm.has_value());
		EXPECT_TRUE(Chunks[1].FirstSequence == 10);
		EXPECT_TRUE(Chunks[1].LastSequence == 19);

		RemoveDatabase(Path);
	}

	void corrupt_chunk_is_truncated_without_losing_earlier_chunks()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			Writer.AppendChunk(MakeChunk("session-b", 0, 10));
			Writer.AppendChunk(MakeChunk("session-b", 10, 10));
		}
		{
			// Corrupt the second (tail) chunk's payload directly, bypassing
			// the writer, to simulate an incomplete/corrupted write.
			LocalData::Private::FSqliteConnection Connection(Path);
			Connection.BeginImmediate();
			auto Statement = Connection.Prepare(
				"UPDATE sample_chunks SET payload_blob = 'corrupt' WHERE "
				"session_id = 'session-b' AND first_sequence = 10;");
			Statement.Step();
			Connection.Commit();
		}

		const auto Report = LocalData::ScanAndRecover(Path);
		EXPECT_TRUE(Report.TruncatedChunkCount == 1);
		EXPECT_TRUE(Report.HighestVerifiedSequence == 9);

		const auto Chunks = LocalData::ReadSampleChunks(Path, "session-b");
		EXPECT_TRUE(Chunks.size() == 1);
		EXPECT_TRUE(Chunks[0].FirstSequence == 0);

		RemoveDatabase(Path);
	}

	void duplicate_chunk_ranges_are_deduplicated()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			Writer.AppendChunk(MakeChunk("session-c", 0, 10));
		}
		{
			// A reconnect/retry can re-append an overlapping range; the
			// writer itself doesn't forbid it, so recovery must dedupe.
			LocalData::FLocalDataJournalWriter Writer(Path);
			Writer.AppendChunk(MakeChunk("session-c", 5, 10));
		}

		const auto Report = LocalData::ScanAndRecover(Path);
		EXPECT_TRUE(Report.DuplicateChunkCount == 1);

		const auto Chunks = LocalData::ReadSampleChunks(Path, "session-c");
		EXPECT_TRUE(Chunks.size() == 1);
		EXPECT_TRUE(Chunks[0].FirstSequence == 0);

		RemoveDatabase(Path);
	}

	void
	unclean_exit_without_terminal_event_is_marked_recovered()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			LocalData::FJournalEvent Started;
			Started.SessionId = "session-d";
			Started.Sequence = 0;
			Started.Kind = LocalData::EJournalEventKind::Started;
			Writer.RecordJournalEvent(Started);
			Writer.AppendChunk(MakeChunk("session-d", 0, 10));
			// No terminal event: simulates a crash mid-workout.
		}

		const auto FirstReport = LocalData::ScanAndRecover(Path);
		EXPECT_TRUE(FirstReport.RecoveredAfterUncleanExit);
		EXPECT_TRUE(FirstReport.HighestVerifiedSequence == 9);

		// Idempotent: recovering an already-recovered file must not add a
		// second marker or otherwise change the outcome.
		const auto SecondReport = LocalData::ScanAndRecover(Path);
		EXPECT_TRUE(SecondReport.RecoveredAfterUncleanExit);
		EXPECT_TRUE(FirstReport.NewlyRecoveredSessionCount == 1);
		EXPECT_TRUE(SecondReport.NewlyRecoveredSessionCount == 0);

		RemoveDatabase(Path);
	}

	void completed_session_is_not_marked_recovered()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			LocalData::FJournalEvent Started;
			Started.SessionId = "session-e";
			Started.Sequence = 0;
			Started.Kind = LocalData::EJournalEventKind::Started;
			Writer.RecordJournalEvent(Started);
			Writer.AppendChunk(MakeChunk("session-e", 0, 10));

			LocalData::FJournalEvent Completed;
			Completed.SessionId = "session-e";
			Completed.Sequence = 1;
			Completed.Kind = LocalData::EJournalEventKind::Completed;
			Writer.StageFinalEvent(Completed);
			Writer.CommitStagedFinalEvent();
		}

		const auto Report = LocalData::ScanAndRecover(Path);
		EXPECT_TRUE(!Report.RecoveredAfterUncleanExit);

		RemoveDatabase(Path);
	}

	void staged_final_event_without_commit_does_not_survive_reopen()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			LocalData::FJournalEvent Started;
			Started.SessionId = "session-f";
			Started.Sequence = 0;
			Started.Kind = LocalData::EJournalEventKind::Started;
			Writer.RecordJournalEvent(Started);

			LocalData::FJournalEvent Completed;
			Completed.SessionId = "session-f";
			Completed.Sequence = 1;
			Completed.Kind = LocalData::EJournalEventKind::Completed;
			// Stage but never commit, then let the writer be destroyed —
			// approximates a crash between the final write and its commit.
			Writer.StageFinalEvent(Completed);
		}

		LocalData::Private::FSqliteConnection Connection(Path);
		auto Statement = Connection.Prepare(
			"SELECT COUNT(*) FROM journal_events WHERE session_id = "
			"'session-f' AND kind = 'Completed';");
		Statement.Step();
		EXPECT_TRUE(Statement.ColumnInt64(0) == 0);

		RemoveDatabase(Path);
	}

	void local_data_writer_recovers_after_a_failed_seal_in_a_staged_chunk()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			Cipher.bFailNextSeal = true;
			EXPECT_TRUE(ThrowsAny([&]
								  { Writer.StageChunk(MakeChunk("session-j", 0, 10)); }));
			// The failed stage must not leave a transaction open: the writer stays usable.
			Writer.AppendChunk(MakeChunk("session-j", 0, 10));
			LocalData::FJournalEvent Completed;
			Completed.SessionId = "session-j";
			Completed.Kind = LocalData::EJournalEventKind::Completed;
			Writer.RecordJournalEvent(Completed);
		}
		EXPECT_TRUE(LocalData::ReadSampleChunks(Path, "session-j", &Cipher).size() == 1);
		RemoveDatabase(Path);
	}

	void local_data_writer_rolls_back_a_duplicate_event_and_stays_usable()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		LocalData::FLocalDataJournalWriter Writer(Path);
		LocalData::FJournalEvent Started;
		Started.SessionId = "session-k";
		Writer.RecordJournalEvent(Started);
		EXPECT_TRUE(ThrowsAny([&]
							  { Writer.RecordJournalEvent(Started); }));
		Started.Sequence = 1;
		Writer.RecordJournalEvent(Started);
		Writer.AppendChunk(MakeChunk("session-k", 0, 1));
		RemoveDatabase(Path);
	}

	void local_data_reader_with_a_cipher_rejects_plaintext_and_unknown_codecs()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			Writer.AppendChunk(MakeChunk("session-l", 0, 3));
		}
		EXPECT_TRUE(LocalData::ReadSampleChunks(Path, "session-l").size() == 1);
		EXPECT_TRUE(ThrowsAny([&]
							  { LocalData::ReadSampleChunks(Path, "session-l", &Cipher); }));
		EXPECT_TRUE(LocalData::ReadSampleChunks(Path, "session-l", &Cipher, true).size() == 1);
		{
			LocalData::Private::FSqliteConnection Connection(Path);
			Connection.Execute("UPDATE sample_chunks SET codec = 'mystery-v9' WHERE session_id = 'session-l';");
		}
		EXPECT_TRUE(ThrowsAny([&]
							  { LocalData::ReadSampleChunks(Path, "session-l"); }));
		EXPECT_TRUE(ThrowsAny([&]
							  { LocalData::ReadSampleChunks(Path, "session-l", &Cipher, true); }));
		RemoveDatabase(Path);
	}

	void local_data_session_summary_requires_an_existing_session()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
		LocalData::FSessionSummary Summary;
		Summary.Id = MakeSessionId(3);
		EXPECT_TRUE(ThrowsAny([&]
							  { Writer.StageSessionSummary(Summary); }));
		Writer.CreateSession(MakeSessionRecord(Summary.Id));
		Writer.StageSessionSummary(Summary);
		Writer.CommitStagedSessionSummary();
		RemoveDatabase(Path);
	}

	void local_data_abandoned_stage_does_not_wedge_later_writes()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		const FRowingSessionId Id = MakeSessionId(6);
		LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
		Writer.CreateSession(MakeSessionRecord(Id));
		LocalData::FSessionSummary Summary;
		Summary.Id = Id;
		Summary.MetricsPayload = "abandoned";
		Writer.StageSessionSummary(Summary);
		// A second stage is refused while the first is open.
		EXPECT_TRUE(ThrowsAny([&]
							  { Writer.StageSessionSummary(Summary); }));
		Writer.AbandonStaged();
		Writer.AbandonStaged();
		EXPECT_TRUE(!LocalData::ReadLatestSessionSummary(Path, Id, Cipher).has_value());

		Summary.MetricsPayload = "kept";
		Writer.StageSessionSummary(Summary);
		Writer.CommitStagedSessionSummary();
		const auto Latest = LocalData::ReadLatestSessionSummary(Path, Id, Cipher);
		EXPECT_TRUE(Latest.has_value() && Latest->MetricsPayload == "kept");
		RemoveDatabase(Path);
	}

	void local_data_recovery_ends_the_open_sessions_row()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		const FRowingSessionId Id = MakeSessionId(4);
		const FRowingSessionId Other = MakeSessionId(5);
		const std::string Canonical = Id.ToCanonicalString();
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			Writer.CreateSession(MakeSessionRecord(Id));
			Writer.CreateSession(MakeSessionRecord(Other));
			Writer.UpdateSessionState(Id, ERowingSessionState::Active);
			Writer.UpdateSessionState(Other, ERowingSessionState::Active);
			LocalData::FJournalEvent Started;
			Started.SessionId = Canonical;
			Writer.RecordJournalEvent(Started);
			Writer.AppendChunk(MakeChunk(Canonical, 0, 5));
		}
		EXPECT_TRUE(LocalData::ScanAndRecover(Path).RecoveredAfterUncleanExit);
		EXPECT_TRUE(LocalData::ReadSession(Path, Id)->State == ERowingSessionState::Ended);
		EXPECT_TRUE(LocalData::ReadSession(Path, Other)->State == ERowingSessionState::Active);
		EXPECT_TRUE(LocalData::ScanAndRecover(Path).RecoveredAfterUncleanExit);
		EXPECT_TRUE(LocalData::ReadSession(Path, Id)->State == ERowingSessionState::Ended);
		RemoveDatabase(Path);
	}

	void local_data_recovery_handles_every_unterminated_session()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		const FRowingSessionId First = MakeSessionId(6);
		const FRowingSessionId Second = MakeSessionId(7);
		const FRowingSessionId Finished = MakeSessionId(8);
		{
			LocalData::FLocalDataJournalWriter Writer(Path);
			for (const FRowingSessionId &Id : {First, Second, Finished})
			{
				Writer.CreateSession(MakeSessionRecord(Id));
				Writer.UpdateSessionState(Id, ERowingSessionState::Active);
				LocalData::FJournalEvent Started;
				Started.SessionId = Id.ToCanonicalString();
				Writer.RecordJournalEvent(Started);
			}
			// First crashed before any chunk; Second has chunks; Finished committed
			// its Completed event but crashed before the sessions.state update.
			Writer.AppendChunk(MakeChunk(Second.ToCanonicalString(), 0, 5));
			LocalData::FJournalEvent Completed;
			Completed.SessionId = Finished.ToCanonicalString();
			Completed.Sequence = 1;
			Completed.Kind = LocalData::EJournalEventKind::Completed;
			Writer.RecordJournalEvent(Completed);
		}
		EXPECT_TRUE(LocalData::ScanAndRecover(Path).RecoveredAfterUncleanExit);
		for (const FRowingSessionId &Id : {First, Second, Finished})
		{
			EXPECT_TRUE(LocalData::ReadSession(Path, Id)->State == ERowingSessionState::Ended);
		}
		RemoveDatabase(Path);
	}

	void local_data_ended_session_cannot_be_reopened()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		const FRowingSessionId Id = MakeSessionId(9);
		LocalData::FLocalDataJournalWriter Writer(Path);
		Writer.CreateSession(MakeSessionRecord(Id));
		Writer.UpdateSessionState(Id, ERowingSessionState::Ended);
		EXPECT_TRUE(ThrowsAny([&]
							  { Writer.UpdateSessionState(Id, ERowingSessionState::Active); }));
		Writer.UpdateSessionState(Id, ERowingSessionState::Ended);
		RemoveDatabase(Path);
	}

	void local_data_journal_event_payload_is_sealed_when_a_cipher_is_present()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		FFakeCipher Cipher;
		{
			LocalData::FLocalDataJournalWriter Writer(Path, &Cipher);
			LocalData::FJournalEvent Started;
			Started.SessionId = "session-m";
			Started.PayloadBlob = "secret-metrics";
			Writer.RecordJournalEvent(Started);
		}
		LocalData::Private::FSqliteConnection Connection(Path);
		auto Statement = Connection.Prepare("SELECT payload_blob FROM journal_events WHERE session_id = 'session-m';");
		Statement.Step();
		EXPECT_TRUE(Statement.ColumnBlob(0).find("secret-metrics") == std::string::npos);
		RemoveDatabase(Path);
	}

	void local_data_concurrent_openers_migrate_a_v1_database_once()
	{
		const auto Path = MakeTempDatabasePath(__func__);
		{
			LocalData::Private::FSqliteConnection Connection(Path);
			Connection.Execute("CREATE TABLE schema_migrations ("
							   "version INTEGER PRIMARY KEY, checksum TEXT NOT NULL, applied_at TEXT NOT NULL);");
			Connection.Execute("CREATE TABLE journal_events ("
							   "session_id TEXT NOT NULL, sequence INTEGER NOT NULL, monotonic_ns INTEGER NOT NULL,"
							   "kind TEXT NOT NULL, payload_version INTEGER NOT NULL, payload_blob BLOB,"
							   "PRIMARY KEY (session_id, sequence));");
			Connection.Execute("CREATE TABLE sample_chunks ("
							   "session_id TEXT NOT NULL, first_sequence INTEGER NOT NULL, last_sequence INTEGER NOT NULL,"
							   "codec TEXT NOT NULL, crc32c INTEGER NOT NULL, payload_blob BLOB NOT NULL,"
							   "PRIMARY KEY (session_id, first_sequence));");
			Connection.Execute("INSERT INTO schema_migrations VALUES (1, 'v1-journal_events-sample_chunks', datetime('now'));");
		}

		std::atomic<int> Failed{0};
		std::vector<std::thread> Threads;
		for (int Index = 0; Index < 4; ++Index)
		{
			Threads.emplace_back([&]
								 {
				try
				{
					LocalData::ReadSampleChunks(Path, "none");
				}
				catch (const std::exception &)
				{
					++Failed;
				} });
		}
		for (std::thread &Thread : Threads)
			Thread.join();

		EXPECT_TRUE(Failed == 0);
		LocalData::Private::FSqliteConnection Connection(Path);
		EXPECT_TRUE(CountRows(Connection, "SELECT COUNT(*) FROM schema_migrations WHERE version = 4;") == 1);
		RemoveDatabase(Path);
	}
} // namespace

int main()
{
	schema_bootstrap_is_idempotent();
	local_data_schema_migrates_v1_journal_without_data_loss();
	finalized_session_commits_terminal_summary_and_outbox_atomically();
	plaintext_finalized_session_round_trips_and_rejects_unknown_summary_codec();
	local_data_sample_chunk_payload_is_encrypted_at_rest();
	local_data_sealed_chunk_is_bound_to_its_identity();
	local_data_recovery_truncates_corrupt_sealed_chunk_without_a_key();
	local_data_session_create_update_and_read_round_trips();
	local_data_session_summary_is_sealed_and_commits_in_two_phases();
	append_and_read_back_round_trip();
	corrupt_chunk_is_truncated_without_losing_earlier_chunks();
	duplicate_chunk_ranges_are_deduplicated();
	unclean_exit_without_terminal_event_is_marked_recovered();
	completed_session_is_not_marked_recovered();
	staged_final_event_without_commit_does_not_survive_reopen();
	local_data_writer_recovers_after_a_failed_seal_in_a_staged_chunk();
	local_data_writer_rolls_back_a_duplicate_event_and_stays_usable();
	local_data_reader_with_a_cipher_rejects_plaintext_and_unknown_codecs();
	local_data_session_summary_requires_an_existing_session();
	local_data_abandoned_stage_does_not_wedge_later_writes();
	local_data_recovery_ends_the_open_sessions_row();
	local_data_recovery_handles_every_unterminated_session();
	local_data_ended_session_cannot_be_reopened();
	local_data_journal_event_payload_is_sealed_when_a_cipher_is_present();
	local_data_concurrent_openers_migrate_a_v1_database_once();

	if (Failures != 0)
	{
		std::cerr << Failures << " local data assertion(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "local data journal tests passed\n";
	return EXIT_SUCCESS;
}
