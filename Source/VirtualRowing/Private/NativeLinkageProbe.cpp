#include "NativeLinkageProbe.h"

#include "LocalData/LocalDataJournal.h"
#include "WorkoutRuntime/LocalDataJournalSink.h"
#include "WorkoutRuntime/WorkoutSession.h"
#include "pm5_sim/MockRowingMachine.h"
#include "pm5_sim/TelemetryFixtures.h"

#include <filesystem>
#include <memory>
#include <system_error>

namespace
{
	// SQLite WAL mode leaves -wal/-shm sidecars next to the database.
	void RemoveDatabaseFiles(const std::filesystem::path &DatabasePath)
	{
		std::error_code Ignored;
		std::filesystem::remove(DatabasePath, Ignored);
		std::filesystem::remove(DatabasePath.string() + "-wal", Ignored);
		std::filesystem::remove(DatabasePath.string() + "-shm", Ignored);
	}
} // namespace

bool VirNativeLinkageProbe()
{
	// The probe runs during module startup: any failure must surface as `false`,
	// never as an exception out of StartupModule.
	std::filesystem::path DatabasePath;
	try
	{
		DatabasePath = std::filesystem::temp_directory_path() / "vir-native-linkage-probe.sqlite";
		RemoveDatabaseFiles(DatabasePath);
		bool bEnded = false;
		{
			LocalData::FLocalDataJournalWriter Writer(DatabasePath);
			FLocalDataJournalSink Sink(Writer);
			pm5_sim::FMockRowingMachine Machine(pm5_sim::MakeSyntheticIndoorRowerScenario());
			FWorkoutSessionDependencies Dependencies;
			Dependencies.Machine = &Machine;
			Dependencies.Sink = &Sink;
			Dependencies.UnixTimeMs = []() -> std::uint64_t
			{ return 1; };
			Dependencies.RandomByte = []() -> std::uint8_t
			{ return 7; };
			FWorkoutSession Session(std::move(Dependencies));
			Session.Tick(0);
			Session.Abort(1);
			bEnded = Session.GetSnapshot().Revision > 0;
			Machine.Shutdown();
		}
		// The writer is closed above, so the files can go.
		RemoveDatabaseFiles(DatabasePath);
		return bEnded;
	}
	catch (...)
	{
		if (!DatabasePath.empty())
			RemoveDatabaseFiles(DatabasePath);
		return false;
	}
}
