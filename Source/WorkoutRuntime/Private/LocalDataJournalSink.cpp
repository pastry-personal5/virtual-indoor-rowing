#include "WorkoutRuntime/LocalDataJournalSink.h"

void FLocalDataJournalSink::CreateSession(const LocalData::FSessionRecord &Session)
{
	Writer.CreateSession(Session);
}

void FLocalDataJournalSink::UpdateSessionState(const FRowingSessionId &Id, ERowingSessionState State)
{
	Writer.UpdateSessionState(Id, State);
}

void FLocalDataJournalSink::RecordEvent(const FRowingSessionId &Id, LocalData::EJournalEventKind Kind, std::uint64_t Sequence, std::uint64_t MonotonicNs, const std::string &Payload)
{
	LocalData::FJournalEvent Event;
	Event.SessionId = Id.ToCanonicalString();
	Event.Sequence = Sequence;
	Event.MonotonicNs = MonotonicNs;
	Event.Kind = Kind;
	Event.PayloadBlob = Payload;
	Writer.RecordJournalEvent(Event);
}

void FLocalDataJournalSink::RecordCapability(const FRowingSessionId &Id, std::uint64_t Sequence, std::uint64_t MonotonicNs, const FRowingMachineInfo &Info)
{
	Writer.RecordCapabilityObserved(Id.ToCanonicalString(), Sequence, MonotonicNs, Info);
}

void FLocalDataJournalSink::AppendSamples(const FRowingSessionId &Id, const std::vector<FRowingMetricSample> &Samples)
{
	LocalData::FSampleChunk Chunk;
	Chunk.SessionId = Id.ToCanonicalString();
	Chunk.FirstSequence = Samples.front().Sequence;
	Chunk.LastSequence = Samples.back().Sequence;
	Chunk.Samples = Samples;
	Writer.AppendChunk(Chunk);
}

void FLocalDataJournalSink::WriteSummary(const FRowingSessionId &Id, const std::string &Payload, std::uint32_t QualityFlags)
{
	LocalData::FSessionSummary Summary;
	Summary.Id = Id;
	Summary.Revision = NextSummaryRevision;
	Summary.MetricsPayload = Payload;
	Summary.QualityFlags = QualityFlags;
	Writer.StageSessionSummary(Summary);
	Writer.CommitStagedSessionSummary();
	++NextSummaryRevision;
}
