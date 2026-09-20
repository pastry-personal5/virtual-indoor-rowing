#pragma once

#include "WorkoutRuntime/AppJournal.h"
#include "WorkoutRuntime/DeviceConnector.h"
#include "WorkoutRuntime/LatencyStats.h"
#include "WorkoutRuntime/NoOpJournalSink.h"
#include "WorkoutRuntime/WorkoutSession.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The real-device path of the app (Phase 1 Milestone 7,
// docs/phase-1/07-milestone-7-real-pm5-app-wiring.md): connection flow, the local
// journal and its failure policy, the session over the attached machine, and the
// latency aggregate. Engine-independent, single-threaded and poll-driven: the
// Unreal subsystem owns one and calls Pump() every tick on the game thread, and
// everything Apple-specific arrives through dependency factories, so this is
// unit-tested with fakes. The cipher factory remains only as a source-compatible
// legacy dependency while ADR-0012 journals are plaintext.
//
// Nothing here touches Bluetooth or the Keychain until Connect(): construction only
// looks for an interrupted session in an existing journal (which needs no key).

enum class ERealDeviceMode : std::uint8_t
{
	// Nothing started; the panel offers Connect.
	Idle,
	// The journal could not be opened; waiting for the user to confirm rowing
	// without saving, retry, or cancel. No discovery exists in this mode.
	JournalDecision,
	Starting,
	Scanning,
	// A discovery-level problem (Bluetooth permission or power, nothing found).
	Problem,
	// A machine is attached; the session snapshot carries everything else.
	Attached
};

enum class EAppJournalStatus : std::uint8_t
{
	// No real-device flow has run yet.
	NotStarted,
	Saving,
	// The user chose to row without saving after the journal failed to open.
	NotSaving
};

struct FRealDeviceDependencies
{
	FDeviceConnector::FDiscoveryFactory MakeDiscovery;
	FAppJournal::FCipherFactory MakeCipher;
	// The app's data directory; the journal is <dir>/journal and metrics are
	// <dir>/metrics. Never inside the repository.
	std::filesystem::path AppDataDirectory;
	// Monotonic nanoseconds on the same timebase as the adapter's event timestamps.
	std::function<std::uint64_t()> Clock;
	std::function<std::uint64_t()> UnixTimeMs;
	std::function<std::uint8_t()> RandomByte;
	std::string SourceRevision;
	// False in a build that must never touch Bluetooth (the Unreal Editor). Connect()
	// then reports EDeviceProblem::TransportUnavailable before it opens the journal
	// or creates a discovery.
	bool bTransportAvailable = true;
};

class FRealDeviceController final
{
  public:
	explicit FRealDeviceController(FRealDeviceDependencies InDependencies);
	// Destruction only releases runtime resources. It never creates a terminal
	// journal event; an unclean process exit must remain recoverable.
	~FRealDeviceController();

	FRealDeviceController(const FRealDeviceController &) = delete;
	FRealDeviceController &operator=(const FRealDeviceController &) = delete;

	// User-initiated start. Opens the owner-only journal first, then creates the
	// discovery. On a journal failure it stops in
	// JournalDecision and returns false; calling Connect() again retries.
	bool Connect();
	// From JournalDecision: proceed without a journal. The row is then not saved and
	// GetJournalStatus() says so for as long as this controller lives.
	bool ConfirmRowWithoutSaving();
	// Leaves JournalDecision, or ends the session and tears the connection down.
	void CancelConnect();

	bool ScanForDevices();
	bool SelectDevice(std::size_t Index);
	// Selects by the token GetCandidateTokens() reported, which survives the list being
	// re-sorted by signal strength between the user seeing it and clicking.
	bool SelectDeviceByToken(std::uint64_t Token);
	// Ends the session, disconnects, forgets the remembered PM5, and offers a scan.
	void ForgetDevice();

	// Ends an active session with runtime semantics (Completed, or Interrupted when
	// it never started or lost its link). Returns false when there is no live session.
	bool EndSession();
	// Explicit application shutdown path. Ends an active session cleanly, writes the
	// latency aggregate, then disconnects. Destruction intentionally does not call it.
	void ShutdownGracefully();
	// A fresh session over the still-attached machine; only valid once the previous
	// one has ended.
	bool StartNewSession();

	// True from the end of a row that was still in progress on the PM5 until the PM5
	// reports the row stopped. Metric samples are held back from the session meanwhile, so
	// a session started after an End mid-row does not journal the same workout twice.
	bool IsAwaitingRowStop() const noexcept
	{
		return bAwaitingRowStop;
	}

	// Adopts a machine, drains its events into the session (the only poller), and
	// ticks the session.
	void Pump();

	ERealDeviceMode GetMode() const;
	EDeviceProblem GetProblem() const
	{
		return bTransportUnavailable ? EDeviceProblem::TransportUnavailable : Connector.GetProblem();
	}
	// Candidate labels, nearest first, only while scanning.
	std::vector<std::string> GetCandidateLabels() const;
	// Parallel to GetCandidateLabels().
	std::vector<std::uint64_t> GetCandidateTokens() const;
	EAppJournalStatus GetJournalStatus() const noexcept
	{
		return JournalStatus;
	}
	// Why the journal is unavailable while in JournalDecision; empty otherwise.
	const std::string &GetJournalError() const noexcept
	{
		return JournalError;
	}
	// Set once at construction: an interrupted session from an earlier run was found.
	bool WasInterruptedSessionRecovered() const noexcept
	{
		return Recovery.bRecoveredInterruptedSession;
	}
	const FWorkoutSnapshot *GetSnapshot() const;
	bool HasSession() const noexcept
	{
		return Session != nullptr;
	}
	// Increases whenever the panel-visible state changes.
	std::uint64_t GetPanelGeneration() const noexcept;

	// Latency: the subsystem calls DiscardPendingLatency() after a pump that did not
	// change the display, and NoteDisplayApplied() when the HUD applies a new one.
	void DiscardPendingLatency() noexcept
	{
		PendingSampleTimestampNs = 0;
	}
	void NoteDisplayApplied(std::uint64_t NowNs);
	const FLatencyStats &GetLatency() const noexcept
	{
		return Latency;
	}
	// Writes the aggregate to the session journal and <dir>/metrics, then resets it;
	// returns the error text or empty (also empty when nothing was recorded).
	std::string FlushLatency();

  private:
	bool StartConnector();
	void EndAndDropSession();
	void CreateSession();
	void ArmRowStopIfRowEnded();
	IJournalSink &ActiveSink();
	std::string FlushLatencyForSession(const FRowingSessionId &SessionId);

	FRealDeviceDependencies Deps;
	FAppJournalRecovery Recovery;
	// Declaration order is destruction order in reverse: session, connector (machine,
	// then discovery), no-op sink, journal.
	std::unique_ptr<FAppJournal> Journal;
	FNoOpJournalSink NoOpSink;
	FDeviceConnector Connector;
	std::unique_ptr<FWorkoutSession> Session;

	EAppJournalStatus JournalStatus = EAppJournalStatus::NotStarted;
	std::string JournalError;
	bool bJournalDecisionPending = false;
	bool bTransportUnavailable = false;
	std::uint64_t SeenMachineGeneration = 0;
	// The attached machine's latest MachineInfo announcement, replayed into a session
	// started on the same machine (StartNewSession).
	std::optional<FRowingMachineEvent> LastMachineInfoEvent;
	bool bAwaitingRowStop = false;
	// Whether the PM5's latest sample said a row is in progress.
	bool bDeviceRowing = false;
	ERowingSessionState LastSessionState = ERowingSessionState::Created;
	std::uint64_t LocalGeneration = 0;

	FLatencyStats Latency;
	std::uint64_t PendingSampleTimestampNs = 0;
};
