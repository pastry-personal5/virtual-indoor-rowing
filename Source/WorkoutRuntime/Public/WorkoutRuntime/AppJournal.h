#pragma once

#include "LocalData/BlobCipher.h"
#include "LocalData/LocalDataJournal.h"
#include "WorkoutRuntime/JournalSink.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

// The app's sealed workout journal (Phase 1 Milestone 7,
// docs/phase-1/07-milestone-7-real-pm5-app-wiring.md). Engine-independent: the
// Keychain-backed cipher arrives through a factory, so this builds and tests
// without Apple frameworks. Only real-device sessions use it; a simulator run never
// opens one, so it never creates a database or a Keychain item.

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

	// Loads the key through the factory (which may touch the Keychain and may block
	// on a system prompt, so call it at a user-initiated moment), creates the
	// directory if needed, and opens the writer. Never throws.
	static FOpenResult Open(const std::filesystem::path &Directory, const FCipherFactory &MakeCipher);

	~FAppJournal();
	FAppJournal(const FAppJournal &) = delete;
	FAppJournal &operator=(const FAppJournal &) = delete;

	IJournalSink &GetSink() noexcept;

  private:
	FAppJournal() = default;

	// Order matters: the sink references the writer, which references the cipher.
	std::unique_ptr<LocalData::IBlobCipher> Cipher;
	std::unique_ptr<LocalData::FLocalDataJournalWriter> Writer;
	std::unique_ptr<IJournalSink> Sink;
};
