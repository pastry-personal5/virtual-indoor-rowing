#include "WorkoutRuntime/WorkoutDisplay.h"

#include <optional>

namespace
{
	// Beyond this a "pace" is not a rowing pace (the device reports no motion as
	// zero or a very large value); show the placeholder rather than a number.
	constexpr std::uint32_t MaxDisplayPaceMs = 100U * 60U * 1000U;
	constexpr std::uint64_t SparseMetricRetentionNs = 1500ULL * 1000ULL * 1000ULL;

	bool CanRetainMetric(const std::uint64_t ObservedNs, const std::uint64_t CurrentNs)
	{
		return ObservedNs != 0 && CurrentNs >= ObservedNs && CurrentNs - ObservedNs <= SparseMetricRetentionNs;
	}

	std::string TwoDigits(const std::uint64_t Value)
	{
		std::string Text = std::to_string(Value);
		if (Text.size() < 2)
			Text.insert(Text.begin(), '0');
		return Text;
	}

	template <typename T>
	std::string OptionalToString(const std::optional<T> &Value)
	{
		return Value ? std::to_string(*Value) : std::string(WorkoutDisplayPlaceholder);
	}

	EWorkoutDisplayConnection ClassifyConnection(const ERowingConnectionState State, std::string &OutLabel)
	{
		switch (State)
		{
		case ERowingConnectionState::Ready:
			OutLabel = "Connected";
			return EWorkoutDisplayConnection::Good;
		case ERowingConnectionState::Stale:
			OutLabel = "Signal weak";
			return EWorkoutDisplayConnection::Degraded;
		case ERowingConnectionState::Reconnecting:
			OutLabel = "Reconnecting";
			return EWorkoutDisplayConnection::Reconnecting;
		case ERowingConnectionState::DiagnosticOnly:
			OutLabel = "Machine not supported";
			return EWorkoutDisplayConnection::Failed;
		case ERowingConnectionState::Unsupported:
		case ERowingConnectionState::Failed:
		case ERowingConnectionState::PermissionDenied:
			OutLabel = "Connection failed";
			return EWorkoutDisplayConnection::Failed;
		case ERowingConnectionState::Idle:
		case ERowingConnectionState::Scanning:
		case ERowingConnectionState::Connecting:
		case ERowingConnectionState::Discovering:
		case ERowingConnectionState::ReadingIdentity:
		case ERowingConnectionState::Subscribing:
		default:
			OutLabel = "Connecting";
			return EWorkoutDisplayConnection::NotConnected;
		}
	}

	const char *EndedBanner(const std::optional<ERowingSessionDisposition> &Disposition)
	{
		if (!Disposition)
			return "Session ended";
		switch (*Disposition)
		{
		case ERowingSessionDisposition::Completed:
			return "Session complete";
		case ERowingSessionDisposition::Interrupted:
			return "Session interrupted";
		case ERowingSessionDisposition::Aborted:
			return "Session aborted";
		}
		return "Session ended";
	}
} // namespace

std::string FormatWorkoutElapsed(const std::uint64_t ElapsedMs)
{
	const std::uint64_t TotalSeconds = ElapsedMs / 1000U;
	const std::uint64_t Hours = TotalSeconds / 3600U;
	const std::uint64_t Minutes = (TotalSeconds / 60U) % 60U;
	const std::uint64_t Seconds = TotalSeconds % 60U;
	if (Hours > 0)
		return std::to_string(Hours) + ":" + TwoDigits(Minutes) + ":" + TwoDigits(Seconds);
	return std::to_string(Minutes) + ":" + TwoDigits(Seconds);
}

std::string FormatWorkoutPace(const std::uint32_t PaceMsPer500M)
{
	if (PaceMsPer500M == 0 || PaceMsPer500M >= MaxDisplayPaceMs)
		return WorkoutDisplayPlaceholder;
	const std::uint64_t TotalSeconds = (static_cast<std::uint64_t>(PaceMsPer500M) + 500U) / 1000U;
	return std::to_string(TotalSeconds / 60U) + ":" + TwoDigits(TotalSeconds % 60U);
}

FWorkoutDisplay MakeNoDeviceDisplay()
{
	FWorkoutDisplay Display;
	Display.Phase = EWorkoutDisplayPhase::NoDevice;
	Display.Connection = EWorkoutDisplayConnection::NotConnected;
	Display.ConnectionLabel = "No device";
	Display.Distance = WorkoutDisplayPlaceholder;
	Display.Elapsed = WorkoutDisplayPlaceholder;
	Display.Pace = WorkoutDisplayPlaceholder;
	Display.Watts = WorkoutDisplayPlaceholder;
	Display.StrokeRate = WorkoutDisplayPlaceholder;
	Display.Banner = "No device connected";
	return Display;
}

FWorkoutDisplay MakeWorkoutDisplay(const FWorkoutSnapshot &Snapshot)
{
	FWorkoutDisplay Display = MakeNoDeviceDisplay();
	Display.Revision = Snapshot.Revision;
	Display.Banner.clear();
	Display.Connection = ClassifyConnection(Snapshot.ConnectionState, Display.ConnectionLabel);
	Display.bJournalHealthy = Snapshot.bJournalHealthy;

	const bool bEnded = Snapshot.State == ERowingSessionState::Ended;
	Display.bCanEnd = !bEnded;
	Display.bCanStartNew = bEnded;

	if (const std::optional<FRowingMetricSample> &Sample = Snapshot.LatestSample)
	{
		Display.Distance = std::to_string(Sample->DistanceMm / 1000U);
		Display.Elapsed = FormatWorkoutElapsed(Sample->SourceElapsedMs);
		Display.Pace = Sample->PaceMsPer500M ? FormatWorkoutPace(*Sample->PaceMsPer500M) : std::string(WorkoutDisplayPlaceholder);
		if (Sample->PaceMsPer500M)
			Display.PaceObservedMonotonicNs = Sample->ReceivedMonotonicNs;
		// Prefer the per-stroke power; fall back to the device-reported average power
		// (the PM5 reports it continuously, the stroke value only once a stroke has
		// completed). Both are device facts, so neither fallback invents a value.
		Display.Watts = Sample->StrokePowerW ? OptionalToString(Sample->StrokePowerW) : OptionalToString(Sample->AveragePowerW);
		if (Sample->StrokePowerW || Sample->AveragePowerW)
			Display.WattsObservedMonotonicNs = Sample->ReceivedMonotonicNs;
		Display.StrokeRate = Sample->StrokeRateDeciSpm ? std::to_string((*Sample->StrokeRateDeciSpm + 5U) / 10U) : std::string(WorkoutDisplayPlaceholder);
		if (Sample->StrokeRateDeciSpm)
			Display.StrokeRateObservedMonotonicNs = Sample->ReceivedMonotonicNs;
		// A heart rate of zero is a strap that is not reporting, not a heart rate.
		if (Sample->HeartRateBpm && *Sample->HeartRateBpm > 0)
		{
			Display.bHeartRateSupplied = true;
			Display.HeartRate = std::to_string(*Sample->HeartRateBpm);
			Display.HeartRateObservedMonotonicNs = Sample->ReceivedMonotonicNs;
		}
	}

	if (bEnded)
	{
		Display.Phase = EWorkoutDisplayPhase::Ended;
		Display.Banner = EndedBanner(Snapshot.Disposition);
		Display.bValuesStale = false;
		return Display;
	}

	if (Snapshot.bInputFrozen || Snapshot.State == ERowingSessionState::ConnectionLost)
	{
		Display.Phase = EWorkoutDisplayPhase::Disconnected;
		Display.bValuesStale = true;
		// Only claim "last values" when there are some to show.
		Display.Banner = Snapshot.LatestSample ? "Disconnected - showing last values" : "Disconnected";
		return Display;
	}

	Display.bValuesStale = Display.Connection == EWorkoutDisplayConnection::Degraded || Display.Connection == EWorkoutDisplayConnection::Reconnecting;

	if (!Snapshot.LatestSample)
	{
		Display.Phase = EWorkoutDisplayPhase::Waiting;
		Display.Banner = "Waiting for the machine";
		return Display;
	}

	switch (Snapshot.LatestSample->WorkoutState)
	{
	case ERowingWorkoutState::Paused:
		Display.Phase = EWorkoutDisplayPhase::Paused;
		Display.Banner = "Paused";
		break;
	case ERowingWorkoutState::Resting:
		Display.Phase = EWorkoutDisplayPhase::Resting;
		Display.Banner = "Rest";
		break;
	case ERowingWorkoutState::WaitingToBegin:
		// The machine is connected but nobody is rowing; the values shown are the
		// device's idle facts, not a workout in progress.
		Display.Phase = EWorkoutDisplayPhase::Waiting;
		Display.Banner = "Start rowing to begin";
		break;
	default:
		Display.Phase = EWorkoutDisplayPhase::Rowing;
		break;
	}
	return Display;
}

FWorkoutDisplay RetainLiveMetricValues(const FWorkoutDisplay &Previous,
									   FWorkoutDisplay Current,
									   const FRowingMetricSample &CurrentSample)
{
	const bool bLiveWorkout = !Current.bValuesStale &&
							  (Current.Phase == EWorkoutDisplayPhase::Rowing ||
							   Current.Phase == EWorkoutDisplayPhase::Paused ||
							   Current.Phase == EWorkoutDisplayPhase::Resting);
	if (!bLiveWorkout)
		return Current;

	// A missing optional field is not a zero and does not invalidate the last
	// value the PM5 supplied. Explicitly supplied values always win, including a
	// zero pace that intentionally formats as the placeholder.
	if (!CurrentSample.PaceMsPer500M && Previous.Pace != WorkoutDisplayPlaceholder &&
		CanRetainMetric(Previous.PaceObservedMonotonicNs, CurrentSample.ReceivedMonotonicNs))
	{
		Current.Pace = Previous.Pace;
		Current.PaceObservedMonotonicNs = Previous.PaceObservedMonotonicNs;
	}
	if (!CurrentSample.StrokeRateDeciSpm && Previous.StrokeRate != WorkoutDisplayPlaceholder &&
		CanRetainMetric(Previous.StrokeRateObservedMonotonicNs, CurrentSample.ReceivedMonotonicNs))
	{
		Current.StrokeRate = Previous.StrokeRate;
		Current.StrokeRateObservedMonotonicNs = Previous.StrokeRateObservedMonotonicNs;
	}
	if (!CurrentSample.StrokePowerW && !CurrentSample.AveragePowerW && Previous.Watts != WorkoutDisplayPlaceholder &&
		CanRetainMetric(Previous.WattsObservedMonotonicNs, CurrentSample.ReceivedMonotonicNs))
	{
		Current.Watts = Previous.Watts;
		Current.WattsObservedMonotonicNs = Previous.WattsObservedMonotonicNs;
	}
	if (!CurrentSample.HeartRateBpm && Previous.bHeartRateSupplied &&
		CanRetainMetric(Previous.HeartRateObservedMonotonicNs, CurrentSample.ReceivedMonotonicNs))
	{
		Current.bHeartRateSupplied = true;
		Current.HeartRate = Previous.HeartRate;
		Current.HeartRateObservedMonotonicNs = Previous.HeartRateObservedMonotonicNs;
	}
	return Current;
}
