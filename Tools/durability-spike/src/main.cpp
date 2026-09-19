// Phase 0 Milestone 4 Spike B kill/recover harness
// (docs/phase-0/07-milestone-4-spikes.md). Not a product tool: proves the
// LocalData WAL journal (Source/LocalData) survives a process kill at each
// of the three boundary classes ADR-0004 names, then reports recovery
// state for Tests/Integration/durability_spike_kill_recover.py to assert
// on. Needs no real PM5 and no Unreal runtime.

#include "LocalData/LocalDataJournal.h"
#include "RowingSim/TelemetryFixtures.h"

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
	constexpr std::uint64_t ChunkSize = 10;
	constexpr std::uint64_t BoundaryChunkIndex = 3;
	constexpr const char *SessionId = "durability-spike-session";

	void PrintUsage()
	{
		std::cout
			<< "usage:\n"
			   "  durability-spike --db <path> --run --kill-at "
			   "<mid-chunk|between-chunks|final-summary> [--sample-count N]\n"
			   "  durability-spike --db <path> --recover\n";
	}

	[[noreturn]] void CrashNow()
	{
		std::raise(SIGKILL);
		std::_Exit(137);
	}

	std::vector<FRowingMetricSample>
	GenerateSyntheticSamples(std::uint64_t Count)
	{
		const RowingSim::FGoldenTelemetryFixture Fixture =
			RowingSim::MakeEasy30SecondFixture();
		std::vector<FRowingMetricSample> Samples;
		Samples.reserve(Count);
		for (std::uint64_t Index = 0; Index < Count; ++Index)
		{
			FRowingMetricSample Sample =
				Fixture.Frames[Index % Fixture.Frames.size()].Sample;
			Sample.Sequence = Index;
			Samples.push_back(Sample);
		}
		return Samples;
	}

	LocalData::FSampleChunk
	MakeChunk(const std::vector<FRowingMetricSample> &Samples,
			  std::uint64_t FirstSequence,
			  std::uint64_t Count)
	{
		LocalData::FSampleChunk Chunk;
		Chunk.SessionId = SessionId;
		Chunk.FirstSequence = FirstSequence;
		Chunk.LastSequence = FirstSequence + Count - 1;
		Chunk.Samples.assign(Samples.begin() + static_cast<std::ptrdiff_t>(FirstSequence),
							 Samples.begin() + static_cast<std::ptrdiff_t>(FirstSequence + Count));
		return Chunk;
	}

	int RunAndCrash(const std::filesystem::path &DatabasePath,
					const std::string &KillAt,
					std::uint64_t SampleCount)
	{
		if (SampleCount % ChunkSize != 0 ||
			SampleCount / ChunkSize <= BoundaryChunkIndex)
		{
			std::cerr << "--sample-count must be a multiple of " << ChunkSize
					  << " and produce more than " << BoundaryChunkIndex
					  << " chunks\n";
			return EXIT_FAILURE;
		}

		LocalData::FLocalDataJournalWriter Writer(DatabasePath);

		LocalData::FJournalEvent Started;
		Started.SessionId = SessionId;
		Started.Sequence = 0;
		Started.Kind = LocalData::EJournalEventKind::Started;
		Writer.RecordJournalEvent(Started);

		const std::vector<FRowingMetricSample> Samples =
			GenerateSyntheticSamples(SampleCount);
		const std::uint64_t ChunkCount = SampleCount / ChunkSize;

		for (std::uint64_t ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			const LocalData::FSampleChunk Chunk =
				MakeChunk(Samples, ChunkIndex * ChunkSize, ChunkSize);

			if (KillAt == "mid-chunk" && ChunkIndex == BoundaryChunkIndex)
			{
				// Kill while this chunk's own transaction is open but
				// before it commits: the row must never appear on reopen.
				Writer.StageChunk(Chunk);
				CrashNow();
			}

			Writer.AppendChunk(Chunk);

			if (KillAt == "between-chunks" &&
				ChunkIndex + 1 == BoundaryChunkIndex)
			{
				// Kill in the gap between two independent, already-committed
				// chunk writes: no transaction is open at the moment of death.
				CrashNow();
			}
		}

		LocalData::FJournalEvent Completed;
		Completed.SessionId = SessionId;
		Completed.Sequence = 1;
		Completed.Kind = LocalData::EJournalEventKind::Completed;

		if (KillAt == "final-summary")
		{
			// Kill after the terminal event is written but before its
			// transaction commits: the session must recover as unclean.
			Writer.StageFinalEvent(Completed);
			CrashNow();
		}

		Writer.StageFinalEvent(Completed);
		Writer.CommitStagedFinalEvent();
		Writer.Close();
		std::cout << "completed_gracefully samples=" << SampleCount << '\n';
		return EXIT_SUCCESS;
	}

	int Recover(const std::filesystem::path &DatabasePath)
	{
		const LocalData::FLocalDataRecoveryReport Report =
			LocalData::ScanAndRecover(DatabasePath);
		std::cout << "highest_verified_sequence="
				  << Report.HighestVerifiedSequence << '\n';
		std::cout << "truncated_chunk_count=" << Report.TruncatedChunkCount
				  << '\n';
		std::cout << "duplicate_chunk_count=" << Report.DuplicateChunkCount
				  << '\n';
		std::cout << "recovered_after_unclean_exit="
				  << (Report.RecoveredAfterUncleanExit ? "true" : "false")
				  << '\n';
		return EXIT_SUCCESS;
	}
} // namespace

int main(int argc, char **argv)
{
	std::vector<std::string> Args(argv + 1, argv + argc);
	std::optional<std::filesystem::path> DatabasePath;
	bool RunMode = false;
	bool RecoverMode = false;
	std::string KillAt;
	std::uint64_t SampleCount = 50;

	for (std::size_t Index = 0; Index < Args.size(); ++Index)
	{
		const std::string &Arg = Args[Index];
		if (Arg == "--help")
		{
			PrintUsage();
			return EXIT_SUCCESS;
		}
		if (Arg == "--db" && Index + 1 < Args.size())
		{
			DatabasePath = Args[++Index];
		}
		else if (Arg == "--run")
		{
			RunMode = true;
		}
		else if (Arg == "--recover")
		{
			RecoverMode = true;
		}
		else if (Arg == "--kill-at" && Index + 1 < Args.size())
		{
			KillAt = Args[++Index];
		}
		else if (Arg == "--sample-count" && Index + 1 < Args.size())
		{
			SampleCount = std::stoull(Args[++Index]);
		}
		else
		{
			std::cerr << "unknown option: " << Arg << '\n';
			PrintUsage();
			return EXIT_FAILURE;
		}
	}

	if (!DatabasePath.has_value() || RunMode == RecoverMode)
	{
		PrintUsage();
		return EXIT_FAILURE;
	}

	if (RunMode)
	{
		if (KillAt != "mid-chunk" && KillAt != "between-chunks" &&
			KillAt != "final-summary")
		{
			std::cerr << "--kill-at must be mid-chunk, between-chunks, or "
						 "final-summary\n";
			return EXIT_FAILURE;
		}
		return RunAndCrash(*DatabasePath, KillAt, SampleCount);
	}
	return Recover(*DatabasePath);
}
