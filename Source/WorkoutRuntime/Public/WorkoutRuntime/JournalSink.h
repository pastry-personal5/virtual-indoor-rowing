#pragma once

#include "LocalData/LocalDataJournal.h"
#include "RowingCore/RowingSession.h"
#include "RowingCore/RowingTelemetry.h"
#include "RowingDevice/RowingMachineTypes.h"

#include <cstdint>
#include <string>
#include <vector>

// The synchronous persistence seam FWorkoutSession writes through
// (docs/phase-1/04-milestone-4-workout-runtime.md). Every method either
// commits or throws; FWorkoutSession catches a throw, counts it, and keeps the
// session running, because a journal failure must not end a local row.
// Implementations are single-threaded and are called only from Tick()/End().
class IJournalSink
{
  public:
	virtual ~IJournalSink() = default;

	// Called once, when the session first becomes Active.
	virtual void CreateSession(const LocalData::FSessionRecord &Session) = 0;
	virtual void UpdateSessionState(const FRowingSessionId &Id, ERowingSessionState State) = 0;
	virtual void RecordEvent(const FRowingSessionId &Id,
							 LocalData::EJournalEventKind Kind,
							 std::uint64_t Sequence,
							 std::uint64_t MonotonicNs,
							 const std::string &Payload) = 0;
	virtual void RecordCapability(const FRowingSessionId &Id,
								  std::uint64_t Sequence,
								  std::uint64_t MonotonicNs,
								  const FRowingMachineInfo &Info) = 0;
	// Samples are in ascending Sequence order and non-empty.
	virtual void AppendSamples(const FRowingSessionId &Id, const std::vector<FRowingMetricSample> &Samples) = 0;
	virtual void WriteSummary(const FRowingSessionId &Id, const std::string &Payload, std::uint32_t QualityFlags) = 0;

	// Terminal persistence. Implementations with transactional storage override
	// this to commit all terminal facts together; the compatibility default keeps
	// existing simulator/test sinks usable while they migrate.
	virtual void FinalizeSession(const FRowingSessionId &Id,
								 LocalData::EJournalEventKind TerminalKind,
								 std::uint64_t Sequence,
								 std::uint64_t MonotonicNs,
								 const std::string &EventPayload,
								 const std::string &SummaryPayload,
								 std::uint32_t QualityFlags)
	{
		RecordEvent(Id, TerminalKind, Sequence, MonotonicNs, EventPayload);
		UpdateSessionState(Id, ERowingSessionState::Ended);
		WriteSummary(Id, SummaryPayload, QualityFlags);
	}
};
