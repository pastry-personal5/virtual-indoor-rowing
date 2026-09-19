#include "WorkoutRuntime/WorkoutSession.h"

#include "WorkoutRuntime/SessionWireMapping.h"

#include <algorithm>
#include <ctime>
#include <exception>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace
{
	constexpr std::uint64_t NanosecondsPerMillisecond = 1000000ULL;

	// Samples the device adapter flagged as unusable never reach the journal or
	// the snapshot. The other quality flags (MissingField, SourceGap,
	// DeviceReconnected, LateCorrection, Outlier) are provenance the record keeps.
	constexpr FRowingQualityFlags RejectedQualityFlags =
		ToRowingQualityFlags(ERowingQualityFlag::Duplicate) |
		ToRowingQualityFlags(ERowingQualityFlag::TimeRegression) |
		ToRowingQualityFlags(ERowingQualityFlag::DistanceRegression) |
		ToRowingQualityFlags(ERowingQualityFlag::UnsupportedValue);

	// A connected PM5 that has not started rowing still streams samples, so
	// activation needs the device to say a workout is running.
	bool IndicatesRowing(const FRowingMetricSample &Sample)
	{
		return Sample.WorkoutState == ERowingWorkoutState::Active && Sample.RowingState != ERowingState::Inactive;
	}

	// Whether the device's own workout state says the workout is over. Only meaningful while the
	// session is Active, which needed an Active workout state to begin with: a PM5 that ends a
	// Just Row can report Complete/Terminated, or drop straight back to WaitingToBegin.
	std::optional<ERowingSessionStateReason> DeviceEndReason(const FRowingMetricSample &Sample)
	{
		switch (Sample.WorkoutState)
		{
		case ERowingWorkoutState::Complete:
			return ERowingSessionStateReason::DeviceCompleted;
		case ERowingWorkoutState::WaitingToBegin:
			// WaitingToBegin is also the PM5's ordinary pre-row state. Treat it as the
			// end of an active row only when the same fact says rowing has stopped.
			return Sample.RowingState == ERowingState::Inactive
					   ? std::optional<ERowingSessionStateReason>(ERowingSessionStateReason::DeviceCompleted)
					   : std::nullopt;
		case ERowingWorkoutState::Terminated:
			return ERowingSessionStateReason::DeviceTerminated;
		default:
			return std::nullopt;
		}
	}

	std::string FormatUtc(std::uint64_t UnixTimeMs)
	{
		const std::time_t Seconds = static_cast<std::time_t>(UnixTimeMs / 1000);
		std::tm Parts{};
		gmtime_r(&Seconds, &Parts);
		char Buffer[32];
		std::strftime(Buffer, sizeof(Buffer), "%Y-%m-%dT%H:%M:%SZ", &Parts);
		return Buffer;
	}

	LocalData::EJournalEventKind TerminalEventKind(ERowingSessionDisposition Disposition)
	{
		switch (Disposition)
		{
		case ERowingSessionDisposition::Completed:
			return LocalData::EJournalEventKind::Completed;
		case ERowingSessionDisposition::Interrupted:
			return LocalData::EJournalEventKind::Interrupted;
		case ERowingSessionDisposition::Aborted:
			return LocalData::EJournalEventKind::Aborted;
		}
		return LocalData::EJournalEventKind::Aborted;
	}
} // namespace

struct FWorkoutSession::FImpl
{
	FImpl(FWorkoutSessionDependencies InDeps, FWorkoutSessionConfig InConfig)
		: Deps(std::move(InDeps)),
		  Config(std::move(InConfig)),
		  Machine(MakeSessionId(Deps))
	{
		Snapshot.SessionId = Machine.GetId();
		Snapshot.State = Machine.GetState();
		Snapshot.ConnectionState = Deps.Machine->GetConnectionState();
		bDeviceReady = Snapshot.ConnectionState == ERowingConnectionState::Ready;
	}

	static FRowingSessionId MakeSessionId(const FWorkoutSessionDependencies &Deps)
	{
		if (Deps.Machine == nullptr || Deps.Sink == nullptr || !Deps.UnixTimeMs || !Deps.RandomByte)
			throw std::invalid_argument("FWorkoutSession requires a machine, a journal sink, a wall clock, and an entropy source");
		return FRowingSessionId::GenerateV7(Deps.UnixTimeMs(), Deps.RandomByte);
	}

	// Journal calls never end the session: a failure is counted and surfaced in
	// the snapshot, and the local row keeps running. Returns whether it succeeded.
	template <typename FWrite>
	bool Journal(FWrite &&Write)
	{
		try
		{
			Write();
			return true;
		}
		catch (const std::exception &)
		{
			++Snapshot.JournalErrorCount;
			Snapshot.bJournalHealthy = false;
			bDirty = true;
			return false;
		}
	}

	// Returns true if the buffer is empty afterwards. A failed write keeps the
	// samples for the next attempt, bounded by MaxBufferedSamples.
	bool Flush(std::uint64_t NowNs)
	{
		if (Buffer.empty())
			return true;
		EnsureSessionRow();
		if (Journal([&]
					{ Deps.Sink->AppendSamples(Machine.GetId(), Buffer); }))
		{
			Buffer.clear();
			return true;
		}
		BufferStartNs = NowNs;
		return false;
	}

	void DropExcessBuffered()
	{
		if (Buffer.size() <= Config.MaxBufferedSamples)
			return;
		const std::size_t Excess = Buffer.size() - Config.MaxBufferedSamples;
		Buffer.erase(Buffer.begin(), Buffer.begin() + static_cast<std::ptrdiff_t>(Excess));
		Snapshot.DroppedSampleCount += Excess;
		bDirty = true;
	}

	// Event rows reference the sessions row. While its creation is still pending
	// the write is held (with its sequence number) and replayed, in order, once
	// the row lands, so a transient CreateSession failure cannot drop Started.
	template <typename FWrite>
	void WriteAfterSessionRow(FWrite &&Write)
	{
		if (PendingRecord)
			DeferredWrites.emplace_back(std::forward<FWrite>(Write));
		else
			Journal(Write);
	}

	void RecordEvent(LocalData::EJournalEventKind Kind, std::uint64_t Ns, const std::string &Payload)
	{
		const std::uint64_t Sequence = ++EventSequence;
		WriteAfterSessionRow([this, Kind, Sequence, Ns, Payload]
							 { Deps.Sink->RecordEvent(Machine.GetId(), Kind, Sequence, Ns, Payload); });
	}

	void RecordCapability(std::uint64_t Ns, const FRowingMachineInfo &Info)
	{
		const std::uint64_t Sequence = ++EventSequence;
		WriteAfterSessionRow([this, Sequence, Ns, Info]
							 { Deps.Sink->RecordCapability(Machine.GetId(), Sequence, Ns, Info); });
	}

	void ApplyChange(const FRowingSessionStateChanged &Change)
	{
		Snapshot.State = Change.NewState;
		Snapshot.Disposition = Change.Disposition;
		bDirty = true;
	}

	// The sessions row every later write depends on. A failed create stays
	// pending and is retried on the next write, so one transient failure cannot
	// lose the workout once the journal recovers.
	void EnsureSessionRow()
	{
		if (!PendingRecord)
			return;
		if (!Journal([&]
					 { Deps.Sink->CreateSession(*PendingRecord); }))
			return;
		PendingRecord.reset();
		std::vector<std::function<void()>> Writes = std::move(DeferredWrites);
		DeferredWrites.clear();
		for (const std::function<void()> &Write : Writes)
			Journal(Write);
	}

	// Created -> Active. The first point at which anything is persisted.
	void Activate(std::uint64_t Ns)
	{
		const auto Change = Machine.TryTransition(ERowingSessionStateReason::DeviceReady);
		if (!Change)
			return;
		ApplyChange(*Change);
		bPersisted = true;

		LocalData::FSessionRecord Record;
		Record.Id = Machine.GetId();
		Record.UserScope = Config.UserScope;
		Record.State = ERowingSessionState::Active;
		Record.StartedAtUtc = FormatUtc(Deps.UnixTimeMs());
		if (!Config.Timezone.empty())
			Record.Timezone = Config.Timezone;
		Record.Source = Config.Source;
		PendingRecord = std::move(Record);
		EnsureSessionRow();
		RecordEvent(LocalData::EJournalEventKind::Started, Ns, {});
		if (PendingInfo)
		{
			RecordCapability(Ns, *PendingInfo);
			PendingInfo.reset();
		}
	}

	// Active <-> ConnectionLost. Flushes first so a state change always
	// lands after the samples that preceded it.
	bool Transition(ERowingSessionStateReason Reason, std::uint64_t Ns)
	{
		const auto Change = Machine.TryTransition(Reason);
		if (!Change)
			return false;
		ApplyChange(*Change);
		if (bPersisted)
		{
			Flush(Ns);
			EnsureSessionRow();
			Journal([&]
					{ Deps.Sink->UpdateSessionState(Machine.GetId(), Change->NewState); });
		}
		return true;
	}

	bool Finish(ERowingSessionStateReason Reason, std::uint64_t Ns)
	{
		const auto Change = Machine.TryTransition(Reason);
		if (!Change)
			return false;
		ApplyChange(*Change);
		Snapshot.EndReason = Reason;
		if (bPersisted)
		{
			EnsureSessionRow();
			if (!Flush(Ns) && !Flush(Ns))
			{
				// The session is over, so there is no later attempt. Say so.
				Snapshot.DroppedSampleCount += Buffer.size();
				Buffer.clear();
			}
			RecordEvent(TerminalEventKind(*Change->Disposition), Ns, {});
			Journal([&]
					{ Deps.Sink->UpdateSessionState(Machine.GetId(), ERowingSessionState::Ended); });
			const FWorkoutSummary Summary = BuildSummary();
			Journal([&]
					{ Deps.Sink->WriteSummary(Machine.GetId(), WorkoutRuntime::Private::SerializeSessionSummary(Summary), QualityUnion); });
		}
		return true;
	}

	void OnLinkLost(std::uint64_t Ns)
	{
		if (Transition(ERowingSessionStateReason::LinkLost, Ns))
			LinkLostAtNs = Ns;
	}

	void OnRestored(std::uint64_t Ns, std::optional<std::uint64_t> DeviceGapMs)
	{
		bRestorePending = false;
		if (Machine.GetState() != ERowingSessionState::ConnectionLost)
			return;
		const std::uint64_t GapMs = DeviceGapMs ? *DeviceGapMs : (Ns >= LinkLostAtNs ? (Ns - LinkLostAtNs) / NanosecondsPerMillisecond : 0);
		Transition(ERowingSessionStateReason::LinkRestored, Ns);
		++Snapshot.GapCount;
		Snapshot.TotalGapMs += GapMs;
		RecordEvent(LocalData::EJournalEventKind::LinkGap, Ns, WorkoutRuntime::Private::SerializeLinkGap({GapMs, DeviceGapMs.has_value()}));
	}

	void OnConnectionState(const FRowingConnectionStateChanged &Changed, std::uint64_t Ns)
	{
		Snapshot.ConnectionState = Changed.NewState;
		bDirty = true;
		bDeviceReady = Changed.NewState == ERowingConnectionState::Ready;
		if (Machine.GetState() == ERowingSessionState::Active && !bDeviceReady)
			OnLinkLost(Ns);
		else if (Machine.GetState() == ERowingSessionState::ConnectionLost)
			// The real adapter emits Ready before FRowingConnectionRestored, which
			// carries the device-measured gap. Wait for it; the next sample or the
			// end of a drain restores host-measured if it never comes.
			bRestorePending = bDeviceReady;
	}

	void OnMachineInfo(const FRowingMachineInfo &Info, std::uint64_t Ns)
	{
		if (bPersisted)
			RecordCapability(Ns, Info);
		else
			PendingInfo = Info;
	}

	void OnSample(const FRowingMetricSample &Sample, std::uint64_t Ns)
	{
		if (Machine.GetState() == ERowingSessionState::Ended)
			return;
		if (bRestorePending)
			OnRestored(Ns, std::nullopt);
		const bool bSequenceRejected = LastSequence && Sample.Sequence <= *LastSequence;
		if (!bSequenceRejected)
			LastSequence = Sample.Sequence;
		const bool bUnusable = (Sample.QualityFlags & RejectedQualityFlags) != 0 ||
							   bSequenceRejected ||
							   // Official input is frozen for the whole gap.
							   Machine.GetState() == ERowingSessionState::ConnectionLost;
		if (bUnusable)
		{
			++Snapshot.RejectedSampleCount;
			bDirty = true;
			// A PM5 that ends the workout may reset distance and time in the same report, which
			// marks the sample as a regression. Its meters are still not used, but the device
			// saying the workout is over is a fact independent of them: without this the row
			// would never end in the HUD.
			// Sequence rejection means this is an old fact and must not end a row.
			// Other rejected samples can still carry a newer, valid PM5 workout-state
			// transition when the device resets its counters at the end.
			if (Machine.GetState() == ERowingSessionState::Active && !bSequenceRejected)
			{
				if (const std::optional<ERowingSessionStateReason> Reason = DeviceEndReason(Sample))
					Finish(*Reason, Ns);
			}
			return;
		}

		Snapshot.LatestSample = Sample;
		bDirty = true;

		if (Machine.GetState() == ERowingSessionState::Created)
		{
			if (!bDeviceReady || !IndicatesRowing(Sample))
				return;
			Activate(Ns);
		}
		Accept(Sample, Ns);
	}

	void Accept(const FRowingMetricSample &Sample, std::uint64_t Ns)
	{
		EnsureSessionRow();
		if (Buffer.empty())
			BufferStartNs = Ns;
		Buffer.push_back(Sample);
		DropExcessBuffered();
		++Snapshot.AcceptedSampleCount;

		QualityUnion |= Sample.QualityFlags;
		if (Sample.StrokeRateDeciSpm && *Sample.StrokeRateDeciSpm > 0)
		{
			StrokeRateSum += *Sample.StrokeRateDeciSpm;
			++StrokeRateCount;
		}
		if (Sample.HeartRateBpm)
			MaxHeartRate = std::max(MaxHeartRate.value_or(0), *Sample.HeartRateBpm);
		if (Sample.AveragePowerW)
			LastAveragePowerW = Sample.AveragePowerW;
		if (Sample.StrokeCount)
			LastStrokeCount = Sample.StrokeCount;
		if (Sample.Calories)
			LastCalories = Sample.Calories;

		if (const std::optional<ERowingSessionStateReason> Reason = DeviceEndReason(Sample))
			Finish(*Reason, Ns);
		else if (Buffer.size() >= Config.FlushSampleCount)
			Flush(Ns);
	}

	// A correction replaces a sample the runtime still holds. Once a sample has
	// flushed the journal is append-only, so the correction is counted and ignored.
	void OnCorrection(const FRowingMetricCorrection &Correction)
	{
		bDirty = true;
		bool bApplied = false;
		for (auto It = Buffer.rbegin(); It != Buffer.rend(); ++It)
		{
			if (It->Sequence == Correction.TargetSampleSequence)
			{
				*It = Correction.CorrectedSample;
				bApplied = true;
				break;
			}
		}
		if (bApplied && Snapshot.LatestSample && Snapshot.LatestSample->Sequence == Correction.TargetSampleSequence)
			Snapshot.LatestSample = Correction.CorrectedSample;
		if (!bApplied)
			++Snapshot.IgnoredCorrectionCount;
	}

	void OnEvent(const FRowingMachineEvent &Event)
	{
		const std::uint64_t Ns = Event.MonotonicTimestampNs;
		if (const auto *Sample = std::get_if<FRowingMetricSample>(&Event.Payload))
			OnSample(*Sample, Ns);
		else if (const auto *Changed = std::get_if<FRowingConnectionStateChanged>(&Event.Payload))
			OnConnectionState(*Changed, Ns);
		else if (const auto *Info = std::get_if<FRowingMachineInfo>(&Event.Payload))
			OnMachineInfo(*Info, Ns);
		else if (std::holds_alternative<FRowingTelemetryStale>(Event.Payload))
			OnLinkLost(Ns);
		else if (const auto *Restored = std::get_if<FRowingConnectionRestored>(&Event.Payload))
		{
			OnMachineInfo(Restored->MachineInfo, Ns);
			OnRestored(Ns, Restored->GapDurationMs);
		}
		else if (const auto *Fault = std::get_if<FRowingFault>(&Event.Payload))
		{
			Snapshot.LastFaultCode = Fault->Code;
			bDirty = true;
		}
		else if (const auto *Correction = std::get_if<FRowingMetricCorrection>(&Event.Payload))
			OnCorrection(*Correction);
		else if (const auto *Stroke = std::get_if<FRowingStrokeMetrics>(&Event.Payload))
		{
			Snapshot.LatestStrokeMetrics = *Stroke;
			bDirty = true;
		}
	}

	FWorkoutSummary BuildSummary() const
	{
		FWorkoutSummary Summary;
		if (Snapshot.LatestSample)
		{
			Summary.TotalDistanceMm = Snapshot.LatestSample->DistanceMm;
			Summary.ElapsedMs = Snapshot.LatestSample->SourceElapsedMs;
		}
		Summary.AcceptedSampleCount = Snapshot.AcceptedSampleCount;
		Summary.RejectedSampleCount = Snapshot.RejectedSampleCount;
		Summary.GapCount = Snapshot.GapCount;
		Summary.TotalGapMs = Snapshot.TotalGapMs;
		if (Summary.TotalDistanceMm > 0 && Summary.ElapsedMs > 0)
			Summary.AveragePaceMsPer500M = static_cast<std::uint32_t>(Summary.ElapsedMs * 500000ULL / Summary.TotalDistanceMm);
		Summary.AveragePowerW = LastAveragePowerW;
		if (StrokeRateCount > 0)
			Summary.AverageStrokeRateDeciSpm = static_cast<std::uint32_t>(StrokeRateSum / StrokeRateCount);
		Summary.MaxHeartRateBpm = MaxHeartRate;
		Summary.StrokeCount = LastStrokeCount;
		Summary.Calories = LastCalories;
		return Summary;
	}

	void Publish()
	{
		Snapshot.bInputFrozen = Machine.GetState() == ERowingSessionState::ConnectionLost;
		if (bDirty)
		{
			++Snapshot.Revision;
			bDirty = false;
		}
	}

	FWorkoutSessionDependencies Deps;
	FWorkoutSessionConfig Config;
	FRowingSessionStateMachine Machine;
	FWorkoutSnapshot Snapshot;

	bool bDirty = false;
	bool bPersisted = false;
	std::optional<LocalData::FSessionRecord> PendingRecord;
	std::vector<std::function<void()>> DeferredWrites;
	bool bDeviceReady = false;
	bool bRestorePending = false;
	std::optional<FRowingMachineInfo> PendingInfo;
	std::uint64_t EventSequence = 0;
	std::optional<std::uint64_t> LastSequence;
	std::uint64_t LinkLostAtNs = 0;

	std::vector<FRowingMetricSample> Buffer;
	std::uint64_t BufferStartNs = 0;

	FRowingQualityFlags QualityUnion = 0;
	std::uint64_t StrokeRateSum = 0;
	std::uint64_t StrokeRateCount = 0;
	std::optional<std::uint32_t> MaxHeartRate;
	std::optional<std::uint32_t> LastAveragePowerW;
	std::optional<std::uint64_t> LastStrokeCount;
	std::optional<std::uint32_t> LastCalories;
};

FWorkoutSession::FWorkoutSession(FWorkoutSessionDependencies Dependencies, FWorkoutSessionConfig Config)
	: Impl(std::make_unique<FImpl>(std::move(Dependencies), std::move(Config)))
{
}

FWorkoutSession::~FWorkoutSession() = default;

void FWorkoutSession::Tick(std::uint64_t NowMonotonicNs)
{
	if (Impl->Machine.GetState() == ERowingSessionState::Ended)
		return;

	FRowingMachineEvent Event;
	std::size_t Drained = 0;
	for (; Impl->Config.bPollMachine && Drained < Impl->Config.MaxEventsPerTick && Impl->Machine.GetState() != ERowingSessionState::Ended && Impl->Deps.Machine->TryPollEvent(Event); ++Drained)
		Impl->OnEvent(Event);

	if (Impl->bRestorePending && Drained < Impl->Config.MaxEventsPerTick)
		Impl->OnRestored(NowMonotonicNs, std::nullopt);

	if (Impl->Machine.GetState() == ERowingSessionState::ConnectionLost && NowMonotonicNs >= Impl->LinkLostAtNs &&
		(NowMonotonicNs - Impl->LinkLostAtNs) / NanosecondsPerMillisecond >= Impl->Config.ReconnectWindowMs)
		Impl->Finish(ERowingSessionStateReason::ReconnectWindowElapsed, NowMonotonicNs);

	if (!Impl->Buffer.empty() && NowMonotonicNs >= Impl->BufferStartNs &&
		(NowMonotonicNs - Impl->BufferStartNs) / NanosecondsPerMillisecond >= Impl->Config.FlushIntervalMs)
		Impl->Flush(NowMonotonicNs);

	Impl->Publish();
}

void FWorkoutSession::Ingest(const FRowingMachineEvent &Event)
{
	if (Impl->Machine.GetState() == ERowingSessionState::Ended)
		return;
	Impl->OnEvent(Event);
	Impl->Publish();
}

bool FWorkoutSession::End(std::uint64_t NowMonotonicNs)
{
	ERowingSessionStateReason Reason = ERowingSessionStateReason::UserAborted;
	if (Impl->Machine.GetState() == ERowingSessionState::Active)
		Reason = ERowingSessionStateReason::UserCompleted;
	const bool bEnded = Impl->Finish(Reason, NowMonotonicNs);
	Impl->Publish();
	return bEnded;
}

bool FWorkoutSession::Abort(std::uint64_t NowMonotonicNs)
{
	const bool bEnded = Impl->Finish(ERowingSessionStateReason::UserAborted, NowMonotonicNs);
	Impl->Publish();
	return bEnded;
}

const FWorkoutSnapshot &FWorkoutSession::GetSnapshot() const noexcept
{
	return Impl->Snapshot;
}

FWorkoutSummary FWorkoutSession::BuildSummary() const
{
	return Impl->BuildSummary();
}
