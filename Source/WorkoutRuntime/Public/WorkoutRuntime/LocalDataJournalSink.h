#pragma once

#include "WorkoutRuntime/JournalSink.h"

// IJournalSink over the product LocalData journal. The writer is not owned and
// must outlive the sink. Summaries require the writer to have been given a
// cipher; without one WriteSummary throws and the runtime counts a journal error.
class FLocalDataJournalSink final : public IJournalSink
{
  public:
	explicit FLocalDataJournalSink(LocalData::FLocalDataJournalWriter &InWriter)
		: Writer(InWriter)
	{
	}

	void CreateSession(const LocalData::FSessionRecord &Session) override;
	void UpdateSessionState(const FRowingSessionId &Id, ERowingSessionState State) override;
	void RecordEvent(const FRowingSessionId &Id, LocalData::EJournalEventKind Kind, std::uint64_t Sequence, std::uint64_t MonotonicNs, const std::string &Payload) override;
	void RecordCapability(const FRowingSessionId &Id, std::uint64_t Sequence, std::uint64_t MonotonicNs, const FRowingMachineInfo &Info) override;
	void AppendSamples(const FRowingSessionId &Id, const std::vector<FRowingMetricSample> &Samples) override;
	void WriteSummary(const FRowingSessionId &Id, const std::string &Payload, std::uint32_t QualityFlags) override;

  private:
	LocalData::FLocalDataJournalWriter &Writer;
	std::uint32_t NextSummaryRevision = 1;
};
