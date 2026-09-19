#pragma once

#include "RowingDevice/IRowingMachine.h"
#include "WorkoutRuntime/JournalSink.h"
#include "WorkoutRuntime/WorkoutSnapshot.h"
#include "WorkoutRuntime/WorkoutSummary.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct FWorkoutSessionConfig
{
	// Outer bound on a link loss: once the session has been ConnectionLost this
	// long it ends Interrupted, whatever the device adapter is still doing.
	std::uint64_t ReconnectWindowMs = 60000;
	// Buffered samples flush to the journal after this many samples or this
	// long, whichever comes first, and on every state change and at end.
	std::size_t FlushSampleCount = 50;
	std::uint64_t FlushIntervalMs = 5000;
	// A journal that keeps failing cannot grow the buffer without bound; the
	// oldest samples are dropped (and counted) past this.
	std::size_t MaxBufferedSamples = 4096;
	// A caller that forwards every event through Ingest() sets this false so
	// Tick() never polls the machine and steals events the caller has not seen.
	bool bPollMachine = true;
	// Bound on events drained from the device per Tick().
	std::size_t MaxEventsPerTick = 4096;
	std::string UserScope = "local";
	std::string Source = "just-row";
	std::string Timezone;
};

struct FWorkoutSessionDependencies
{
	// Neither is owned; both must outlive the session.
	IRowingMachine *Machine = nullptr;
	IJournalSink *Sink = nullptr;
	// Feeds the UUIDv7 session id and the session's UTC start time.
	std::function<std::uint64_t()> UnixTimeMs;
	std::function<std::uint8_t()> RandomByte;
};

// Just Row session orchestrator (FR-004). Engine-independent, single-threaded,
// and poll-driven: the caller owns the clock and calls Tick() with a monotonic
// nanosecond timestamp on the same timebase as the device events. See
// docs/phase-1/04-milestone-4-workout-runtime.md for the contract.
//
// Nothing reaches the journal until the session first becomes Active, so an
// idle or aborted-before-rowing session leaves no row. The caller owns the
// device connection lifecycle (Connect/Disconnect); this class only consumes
// its events.
class FWorkoutSession final
{
  public:
	FWorkoutSession(FWorkoutSessionDependencies Dependencies, FWorkoutSessionConfig Config = {});
	~FWorkoutSession();

	FWorkoutSession(const FWorkoutSession &) = delete;
	FWorkoutSession &operator=(const FWorkoutSession &) = delete;

	// Drains device events, applies the reconnect window, and flushes buffered
	// samples on the time trigger. A no-op once the session has Ended.
	void Tick(std::uint64_t NowMonotonicNs);

	// For a caller that must observe device events itself (pm5-tui): it drains
	// the machine and forwards each event here, then calls Tick(). Two consumers
	// polling one machine would steal each other's events, so a caller that
	// ingests must not let anything else poll the machine. A no-op once Ended.
	void Ingest(const FRowingMachineEvent &Event);

	// User-driven ends. End() completes an Active session, and aborts one that
	// never started or is ConnectionLost (Interrupted). Abort() always aborts.
	// Both return false if the session has already Ended.
	bool End(std::uint64_t NowMonotonicNs);
	bool Abort(std::uint64_t NowMonotonicNs);

	const FWorkoutSnapshot &GetSnapshot() const noexcept;
	// The summary as it will be (or was) sealed at end; usable at any time.
	FWorkoutSummary BuildSummary() const;

  private:
	struct FImpl;
	std::unique_ptr<FImpl> Impl;
};
