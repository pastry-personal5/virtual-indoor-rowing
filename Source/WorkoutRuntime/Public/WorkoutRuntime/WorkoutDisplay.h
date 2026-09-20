#pragma once

#include "WorkoutRuntime/WorkoutSnapshot.h"

#include <cstdint>
#include <string>

// Snapshot-to-display formatting for the FR-003 HUD (Phase 1 Milestone 5,
// docs/phase-1/05-milestone-5-unreal-hud.md). Pure and locale-independent: the
// output uses ASCII digits and fixed separators only, so the Unreal widget binds
// already-formatted values and never formats numbers itself. Nothing here
// invents a value: a metric the device did not report is the fixed placeholder,
// never zero, and values frozen by a link gap are flagged stale, never
// interpolated. The HUD may retain the most recent device fact for a temporarily
// absent optional field while the connection remains live; see
// RetainLiveMetricValues.

// The placeholder every absent metric renders as.
inline constexpr const char *WorkoutDisplayPlaceholder = "--";

enum class EWorkoutDisplayPhase : std::uint8_t
{
	// No machine is attached; there is no snapshot to show.
	NoDevice,
	// A session exists but the device has not yet reported a valid sample.
	Waiting,
	Rowing,
	// The device reports the workout paused or resting; values are live.
	Paused,
	Resting,
	// The link is lost: LatestSample values are the last device facts and are stale.
	Disconnected,
	Ended
};

enum class EWorkoutDisplayConnection : std::uint8_t
{
	// No machine, or the machine has not finished connecting.
	NotConnected,
	Good,
	// Connected but data has stopped arriving; values may be stale.
	Degraded,
	Reconnecting,
	Failed
};

struct FWorkoutDisplay
{
	// Copied from the snapshot so a consumer can skip re-formatting when it is
	// unchanged. Zero for the no-device display.
	std::uint64_t Revision = 0;

	EWorkoutDisplayPhase Phase = EWorkoutDisplayPhase::NoDevice;
	EWorkoutDisplayConnection Connection = EWorkoutDisplayConnection::NotConnected;
	std::string ConnectionLabel;

	// Whole meters, e.g. "1234".
	std::string Distance;
	// "M:SS", or "H:MM:SS" from one hour, of device-reported elapsed time.
	std::string Elapsed;
	// Time per 500 m as "M:SS", rounded to the nearest second.
	std::string Pace;
	// Stroke power when the device supplied it, else the device average power.
	std::string Watts;
	// Whole strokes per minute.
	std::string StrokeRate;
	// Empty unless bHeartRateSupplied.
	std::string HeartRate;
	bool bHeartRateSupplied = false;

	// Presentation-only age markers for bounded sparse-packet retention. They are
	// never rendered or persisted and use the adapter's monotonic timebase.
	std::uint64_t PaceObservedMonotonicNs = 0;
	std::uint64_t WattsObservedMonotonicNs = 0;
	std::uint64_t StrokeRateObservedMonotonicNs = 0;
	std::uint64_t HeartRateObservedMonotonicNs = 0;

	// True when the shown values may be older than the present: the link is lost,
	// stale, or reconnecting. The last device values stay visible, flagged.
	bool bValuesStale = false;

	// Empty when no banner applies; otherwise the disconnected, paused, resting,
	// ended, or waiting text the HUD shows above the metrics.
	std::string Banner;
	// Whether a session can be ended, and whether a new one can be started.
	bool bCanEnd = false;
	bool bCanStartNew = false;

	// Carried through so the HUD can show a journal indicator when a journal is
	// attached. The runtime never attaches one in Milestone 5.
	bool bJournalHealthy = true;
};

// The idle "no device" display: placeholders, no banner beyond the state text.
FWorkoutDisplay MakeNoDeviceDisplay();

FWorkoutDisplay MakeWorkoutDisplay(const FWorkoutSnapshot &Snapshot);

// A PM5 general-status sample can arrive without its optional companion packet.
// Retain the prior displayed device fact only for that short, live omission;
// explicit device values (including an invalid/zero pace), a stale link, and a
// new session are never masked. The caller owns the session-identity boundary.
FWorkoutDisplay RetainLiveMetricValues(const FWorkoutDisplay &Previous,
									   FWorkoutDisplay Current,
									   const FRowingMetricSample &CurrentSample);

// Exposed for tests and for other formatters that must agree with the HUD.
std::string FormatWorkoutElapsed(std::uint64_t ElapsedMs);
std::string FormatWorkoutPace(std::uint32_t PaceMsPer500M);
