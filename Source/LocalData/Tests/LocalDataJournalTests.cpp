#include "LocalData/LocalDataJournal.h"

#include "LocalData/Crc32c.h"
#include "LocalData/Sqlite.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>

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

	std::filesystem::path MakeTempDatabasePath(const char *const TestName)
	{
		std::random_device Random;
		const auto Path = std::filesystem::temp_directory_path() /
						  (std::string("local_data_tests_") + TestName +
						   "_" + std::to_string(Random()) + ".sqlite3");
		std::filesystem::remove(Path);
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
		std::filesystem::remove(Path);
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

		std::filesystem::remove(Path);
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

		std::filesystem::remove(Path);
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

		std::filesystem::remove(Path);
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

		std::filesystem::remove(Path);
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

		std::filesystem::remove(Path);
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

		std::filesystem::remove(Path);
	}
} // namespace

int main()
{
	schema_bootstrap_is_idempotent();
	append_and_read_back_round_trip();
	corrupt_chunk_is_truncated_without_losing_earlier_chunks();
	duplicate_chunk_ranges_are_deduplicated();
	unclean_exit_without_terminal_event_is_marked_recovered();
	completed_session_is_not_marked_recovered();
	staged_final_event_without_commit_does_not_survive_reopen();

	if (Failures != 0)
	{
		std::cerr << Failures << " local data assertion(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "local data journal tests passed\n";
	return EXIT_SUCCESS;
}
