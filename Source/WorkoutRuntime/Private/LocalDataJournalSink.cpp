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
	std::uint32_t &Revision = NextSummaryRevision.try_emplace(Id.ToCanonicalString(), 1).first->second;
	LocalData::FSessionSummary Summary;
	Summary.Id = Id;
	Summary.Revision = Revision;
	Summary.MetricsPayload = Payload;
	Summary.QualityFlags = QualityFlags;
	Writer.StageSessionSummary(Summary);
	try
	{
		Writer.CommitStagedSessionSummary();
	}
	catch (...)
	{
		// The runtime never retries a summary, and a stage that stays open would
		// fail every later write through this writer. One more commit attempt,
		// then give the stage up.
		try
		{
			Writer.CommitStagedSessionSummary();
		}
		catch (...)
		{
			Writer.AbandonStaged();
			throw;
		}
	}
	++Revision;
}
