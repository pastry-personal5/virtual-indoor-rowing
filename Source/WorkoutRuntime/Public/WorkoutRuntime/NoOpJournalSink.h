#pragma once

#include "WorkoutRuntime/JournalSink.h"

// A sink that persists nothing: simulator and automation runs must never create a
// database or a Keychain item, and a real-device row the user explicitly chose to
// run without saving uses it too.
class FNoOpJournalSink final : public IJournalSink
{
  public:
	void CreateSession(const LocalData::FSessionRecord &) override {}
	void UpdateSessionState(const FRowingSessionId &, ERowingSessionState) override {}
	void RecordEvent(const FRowingSessionId &, LocalData::EJournalEventKind, std::uint64_t, std::uint64_t, const std::string &) override {}
	void RecordCapability(const FRowingSessionId &, std::uint64_t, std::uint64_t, const FRowingMachineInfo &) override {}
	void AppendSamples(const FRowingSessionId &, const std::vector<FRowingMetricSample> &) override {}
	void WriteSummary(const FRowingSessionId &, const std::string &, std::uint32_t) override {}
};
