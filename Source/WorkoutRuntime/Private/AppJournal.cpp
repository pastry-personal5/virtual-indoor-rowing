#include "WorkoutRuntime/AppJournal.h"

#include "WorkoutRuntime/LocalDataJournalSink.h"

#include <exception>

namespace
{
	constexpr const char *DatabaseFileName = "workout-journal.sqlite3";
} // namespace

void EnsureOwnerOnlyDirectory(const std::filesystem::path &Directory)
{
	std::filesystem::create_directories(Directory);
	std::filesystem::permissions(Directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
}

std::filesystem::path GetAppJournalDatabasePath(const std::filesystem::path &Directory)
{
	return Directory / DatabaseFileName;
}

FAppJournalRecovery RecoverInterruptedSession(const std::filesystem::path &Directory)
{
	FAppJournalRecovery Result;
	try
	{
		const std::filesystem::path Database = GetAppJournalDatabasePath(Directory);
		if (!std::filesystem::exists(Database))
			return Result;
		Result.bScanned = true;
		Result.bRecoveredInterruptedSession = LocalData::ScanAndRecover(Database).NewlyRecoveredSessionCount > 0;
	}
	catch (const std::exception &Failure)
	{
		Result.Error = Failure.what();
	}
	return Result;
}

FAppJournal::FOpenResult FAppJournal::Open(const std::filesystem::path &Directory, const FCipherFactory &MakeCipher)
{
	FOpenResult Result;
	try
	{
		std::unique_ptr<FAppJournal> Journal(new FAppJournal());
		// ADR-0012: development and single-user journals are deliberately
		// plaintext. Retain the factory argument temporarily for source
		// compatibility with callers while ensuring it is never invoked.
		(void)MakeCipher;
		EnsureOwnerOnlyDirectory(Directory);
		Journal->Writer = std::make_unique<LocalData::FLocalDataJournalWriter>(GetAppJournalDatabasePath(Directory));
		Journal->Sink = std::make_unique<FLocalDataJournalSink>(*Journal->Writer);
		Result.Journal = std::move(Journal);
	}
	catch (const std::exception &Failure)
	{
		Result.Journal.reset();
		Result.Error = Failure.what()[0] != '\0' ? Failure.what() : "the journal could not be opened";
	}
	return Result;
}

FAppJournal::~FAppJournal()
{
	// Members are released in reverse order of construction: sink, then writer.
	Sink.reset();
	Writer.reset();
}

IJournalSink &FAppJournal::GetSink() noexcept
{
	return *Sink;
}

void FAppJournal::RecordSessionLatencySummary(const LocalData::FSessionLatencySummary &Summary)
{
	Writer->RecordSessionLatencySummary(Summary);
}
