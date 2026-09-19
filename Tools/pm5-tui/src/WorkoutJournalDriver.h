#pragma once

#include "RowingDevice/IRowingMachine.h"
#include "WorkoutRuntime/WorkoutSession.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace LocalData
{
	class FLocalDataJournalWriter;
}

namespace LocalDataMac
{
	class FCryptoKitBlobCipher;
}

class FLocalDataJournalSink;

namespace PM5Tui
{
	// Drives an FWorkoutSession from the TUI's device event stream and journals
	// it into a Keychain-sealed LocalData database (Phase 1 Milestone 4,
	// docs/phase-1/04-milestone-4-workout-runtime.md). Opt-in via --journal.
	//
	// The database holds athlete data: it lives in an owner-only directory, is
	// Git-ignored, and must be treated as private evidence. This class only ever
	// reports aggregate state (counts and session state) for logging and display,
	// never raw samples.
	class FWorkoutJournalDriver
	{
	  public:
		// Opens (creating if needed) the sealed journal under Directory and
		// recovers any session a previous run left open. On failure the driver
		// is unavailable and Error() says why; the TUI keeps working without it.
		explicit FWorkoutJournalDriver(const std::filesystem::path &Directory);
		~FWorkoutJournalDriver();

		FWorkoutJournalDriver(const FWorkoutJournalDriver &) = delete;
		FWorkoutJournalDriver &operator=(const FWorkoutJournalDriver &) = delete;

		bool IsAvailable() const noexcept
		{
			return Error.empty();
		}
		const std::string &GetError() const noexcept
		{
			return Error;
		}
		const std::string &GetRecoveryNote() const noexcept
		{
			return RecoveryNote;
		}

		// Binds to the TUI's current machine, replacing the session if it changed.
		// A row still in progress on the previous machine is aborted.
		void SyncMachine(IRowingMachine *Machine, std::uint64_t NowNs);
		// Forward every event the TUI drains from the machine.
		void Ingest(const FRowingMachineEvent &Event);
		// Returns log lines for session-state changes since the last call.
		std::vector<std::string> Tick(std::uint64_t NowNs);
		// User-ended session. Ended mid-row, the next session waits until the PM5
		// reports the row has stopped; otherwise a fresh one starts immediately.
		std::vector<std::string> EndSession(std::uint64_t NowNs);

		std::string StatusText() const;

	  private:
		void StartSession(std::uint64_t NowNs);
		std::vector<std::string> DescribeChange();

		std::string Error;
		std::string RecoveryNote;
		std::unique_ptr<LocalDataMac::FCryptoKitBlobCipher> Cipher;
		std::unique_ptr<LocalData::FLocalDataJournalWriter> Writer;
		std::unique_ptr<FLocalDataJournalSink> Sink;
		IRowingMachine *Machine = nullptr;
		std::unique_ptr<FWorkoutSession> Session;
		std::optional<FRowingMachineInfo> LastMachineInfo;
		bool bAwaitingRowStop = false;
		ERowingSessionState LastLoggedState = ERowingSessionState::Created;
		std::uint32_t LastLoggedGapCount = 0;
	};
} // namespace PM5Tui
