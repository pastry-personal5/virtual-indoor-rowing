#pragma once

#include "LocalData/BlobCipher.h"
#include "LocalData/LocalDataJournal.h"
#include "WorkoutRuntime/JournalSink.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

// The app's owner-only workout journal (Phase 1 Milestone 7,
// docs/phase-1/07-milestone-7-real-pm5-app-wiring.md). Development and
// single-user journals are plaintext under ADR-0012. Only real-device sessions
// use it; a simulator run never opens one or creates a database.

// Creates the directory (and parents) and restricts it to the owner. Throws
// std::filesystem::filesystem_error on failure.
void EnsureOwnerOnlyDirectory(const std::filesystem::path &Directory);

// The database file name inside the journal directory.
std::filesystem::path GetAppJournalDatabasePath(const std::filesystem::path &Directory);

struct FAppJournalRecovery
{
	// A database file existed and was scanned.
	bool bScanned = false;
	// This scan found and marked a session a killed process left open. False on a
	// later launch that only finds the earlier, already-marked recovery.
	bool bRecoveredInterruptedSession = false;
	// Non-empty when the scan itself failed (the journal is left untouched).
	std::string Error;
};

// Scans an existing journal for a session a killed process left open. Needs no
// key and never creates the database or the directory, so it is safe at launch.
FAppJournalRecovery RecoverInterruptedSession(const std::filesystem::path &Directory);

class FAppJournal final
{
  public:
	using FCipherFactory = std::function<std::unique_ptr<LocalData::IBlobCipher>()>;

	struct FOpenResult
	{
		std::unique_ptr<FAppJournal> Journal;
		// Non-empty exactly when Journal is null: why the journal is unavailable.
		std::string Error;
	};

	// Creates the owner-only directory if needed and opens a plaintext development
	// journal per ADR-0012. The factory is retained only for caller compatibility
	// and is never invoked. Never throws.
	static FOpenResult Open(const std::filesystem::path &Directory, const FCipherFactory &MakeCipher);

	~FAppJournal();
	FAppJournal(const FAppJournal &) = delete;
	FAppJournal &operator=(const FAppJournal &) = delete;

	IJournalSink &GetSink() noexcept;
	void RecordSessionLatencySummary(const LocalData::FSessionLatencySummary &Summary);

  private:
	FAppJournal() = default;

	// The sink references the writer.
	std::unique_ptr<LocalData::FLocalDataJournalWriter> Writer;
	std::unique_ptr<IJournalSink> Sink;
};
