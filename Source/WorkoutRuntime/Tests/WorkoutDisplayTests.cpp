#include "WorkoutRuntime/WorkoutDisplay.h"

#include <cstdlib>
#include <iostream>

namespace
{
	int Failures = 0;

	void Expect(const bool Condition, const char *const Expression, const char *const TestName)
	{
		if (!Condition)
		{
			std::cerr << TestName << ": expectation failed: " << Expression << '\n';
			++Failures;
		}
	}

#define EXPECT_TRUE(Expression) Expect((Expression), #Expression, __func__)

	FRowingMetricSample MakeFullSample()
	{
		FRowingMetricSample Sample;
		Sample.Sequence = 12;
		Sample.SourceElapsedMs = 754300;
		Sample.DistanceMm = 2834567;
		Sample.PaceMsPer500M = 125300;
		Sample.StrokeRateDeciSpm = 224;
		Sample.StrokePowerW = 187;
		Sample.HeartRateBpm = 142;
		Sample.WorkoutState = ERowingWorkoutState::Active;
		Sample.RowingState = ERowingState::Active;
		return Sample;
	}

	FWorkoutSnapshot MakeActiveSnapshot()
	{
		FWorkoutSnapshot Snapshot;
		Snapshot.Revision = 9;
		Snapshot.State = ERowingSessionState::Active;
		Snapshot.ConnectionState = ERowingConnectionState::Ready;
		Snapshot.LatestSample = MakeFullSample();
		return Snapshot;
	}

	void workout_display_formats_active_snapshot()
	{
		const FWorkoutDisplay Display = MakeWorkoutDisplay(MakeActiveSnapshot());
		EXPECT_TRUE(Display.Revision == 9);
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Rowing);
		EXPECT_TRUE(Display.Distance == "2834");
		EXPECT_TRUE(Display.Elapsed == "12:34");
		EXPECT_TRUE(Display.Pace == "2:05");
		EXPECT_TRUE(Display.Watts == "187");
		EXPECT_TRUE(Display.StrokeRate == "22");
		EXPECT_TRUE(Display.bHeartRateSupplied && Display.HeartRate == "142");
		EXPECT_TRUE(Display.Connection == EWorkoutDisplayConnection::Good);
		EXPECT_TRUE(Display.ConnectionLabel == "Connected");
		EXPECT_TRUE(!Display.bValuesStale);
		EXPECT_TRUE(Display.Banner.empty());
		EXPECT_TRUE(Display.bCanEnd && !Display.bCanStartNew);
	}

	void workout_display_uses_placeholder_for_absent_metrics()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		Snapshot.LatestSample->PaceMsPer500M.reset();
		Snapshot.LatestSample->StrokePowerW.reset();
		Snapshot.LatestSample->StrokeRateDeciSpm.reset();
		FWorkoutDisplay Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Pace == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(Display.Watts == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(Display.StrokeRate == WorkoutDisplayPlaceholder);
		// Distance and elapsed are always device-reported with a sample, so zero is a real zero.
		EXPECT_TRUE(Display.Distance == "2834");

		// A device pace of zero is "not moving", not a 0:00 split.
		Snapshot.LatestSample->PaceMsPer500M = 0;
		Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Pace == WorkoutDisplayPlaceholder);

		// Before the first sample nothing is invented.
		FWorkoutSnapshot Waiting;
		Waiting.State = ERowingSessionState::Created;
		Waiting.ConnectionState = ERowingConnectionState::Ready;
		Display = MakeWorkoutDisplay(Waiting);
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Waiting);
		EXPECT_TRUE(Display.Distance == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(Display.Elapsed == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(Display.Pace == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(!Display.bHeartRateSupplied);

		const FWorkoutDisplay NoDevice = MakeNoDeviceDisplay();
		EXPECT_TRUE(NoDevice.Phase == EWorkoutDisplayPhase::NoDevice);
		EXPECT_TRUE(NoDevice.Distance == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(!NoDevice.bCanEnd && !NoDevice.bCanStartNew);
	}

	void workout_display_falls_back_to_average_power_when_no_stroke_power()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		Snapshot.LatestSample->StrokePowerW.reset();
		Snapshot.LatestSample->AveragePowerW = 163;
		EXPECT_TRUE(MakeWorkoutDisplay(Snapshot).Watts == "163");

		// The per-stroke value wins when both are reported.
		Snapshot.LatestSample->StrokePowerW = 187;
		EXPECT_TRUE(MakeWorkoutDisplay(Snapshot).Watts == "187");

		Snapshot.LatestSample->StrokePowerW.reset();
		Snapshot.LatestSample->AveragePowerW.reset();
		EXPECT_TRUE(MakeWorkoutDisplay(Snapshot).Watts == WorkoutDisplayPlaceholder);
	}

	void workout_display_marks_values_stale_when_input_frozen()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		Snapshot.State = ERowingSessionState::ConnectionLost;
		Snapshot.bInputFrozen = true;
		Snapshot.ConnectionState = ERowingConnectionState::Reconnecting;
		const FWorkoutDisplay Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Disconnected);
		EXPECT_TRUE(Display.bValuesStale);
		EXPECT_TRUE(!Display.Banner.empty());
		// The last device facts stay visible, unchanged; nothing is interpolated.
		EXPECT_TRUE(Display.Distance == "2834");
		EXPECT_TRUE(Display.Elapsed == "12:34");
		EXPECT_TRUE(Display.Connection == EWorkoutDisplayConnection::Reconnecting);
		EXPECT_TRUE(Display.bCanEnd);

		FWorkoutSnapshot Weak = MakeActiveSnapshot();
		Weak.ConnectionState = ERowingConnectionState::Stale;
		EXPECT_TRUE(MakeWorkoutDisplay(Weak).bValuesStale);
	}

	void workout_display_omits_heart_rate_when_not_supplied()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		Snapshot.LatestSample->HeartRateBpm.reset();
		FWorkoutDisplay Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(!Display.bHeartRateSupplied && Display.HeartRate.empty());

		// A strap that reports zero is not supplying a heart rate.
		Snapshot.LatestSample->HeartRateBpm = 0;
		Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(!Display.bHeartRateSupplied && Display.HeartRate.empty());
	}

	void workout_display_reports_journal_unhealthy()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		EXPECT_TRUE(MakeWorkoutDisplay(Snapshot).bJournalHealthy);
		Snapshot.bJournalHealthy = false;
		Snapshot.JournalErrorCount = 3;
		const FWorkoutDisplay Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(!Display.bJournalHealthy);
		// An unhealthy journal never ends or hides the local row.
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Rowing && Display.bCanEnd);
	}

	void workout_display_formats_pace_and_time_boundaries()
	{
		EXPECT_TRUE(FormatWorkoutElapsed(0) == "0:00");
		EXPECT_TRUE(FormatWorkoutElapsed(999) == "0:00");
		EXPECT_TRUE(FormatWorkoutElapsed(59999) == "0:59");
		EXPECT_TRUE(FormatWorkoutElapsed(60000) == "1:00");
		EXPECT_TRUE(FormatWorkoutElapsed(3599999) == "59:59");
		EXPECT_TRUE(FormatWorkoutElapsed(3600000) == "1:00:00");
		EXPECT_TRUE(FormatWorkoutElapsed(36061000) == "10:01:01");

		EXPECT_TRUE(FormatWorkoutPace(125300) == "2:05");
		EXPECT_TRUE(FormatWorkoutPace(125499) == "2:05");
		EXPECT_TRUE(FormatWorkoutPace(125500) == "2:06");
		EXPECT_TRUE(FormatWorkoutPace(59500) == "1:00");
		EXPECT_TRUE(FormatWorkoutPace(1000) == "0:01");
		EXPECT_TRUE(FormatWorkoutPace(0) == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(FormatWorkoutPace(6000000) == WorkoutDisplayPlaceholder);
		EXPECT_TRUE(FormatWorkoutPace(5999999) == "100:00");
	}

	void workout_display_shows_final_values_after_session_ends()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		Snapshot.State = ERowingSessionState::Ended;
		Snapshot.Disposition = ERowingSessionDisposition::Completed;
		Snapshot.EndReason = ERowingSessionStateReason::UserCompleted;
		FWorkoutDisplay Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Ended);
		EXPECT_TRUE(Display.Distance == "2834" && Display.Elapsed == "12:34");
		EXPECT_TRUE(Display.Banner == "Session complete");
		EXPECT_TRUE(!Display.bValuesStale);
		EXPECT_TRUE(!Display.bCanEnd && Display.bCanStartNew);

		Snapshot.Disposition = ERowingSessionDisposition::Interrupted;
		Snapshot.bInputFrozen = true;
		Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Banner == "Session interrupted");
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Ended);
	}

	void workout_display_shows_waiting_when_device_is_waiting_to_begin()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		Snapshot.LatestSample->WorkoutState = ERowingWorkoutState::WaitingToBegin;
		const FWorkoutDisplay Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Waiting);
		EXPECT_TRUE(!Display.Banner.empty());
		EXPECT_TRUE(Display.bCanEnd);
	}

	void workout_display_disconnected_banner_does_not_claim_values_without_a_sample()
	{
		FWorkoutSnapshot Snapshot;
		Snapshot.State = ERowingSessionState::ConnectionLost;
		Snapshot.bInputFrozen = true;
		FWorkoutDisplay Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Phase == EWorkoutDisplayPhase::Disconnected);
		EXPECT_TRUE(Display.Banner == "Disconnected");
		EXPECT_TRUE(Display.Distance == WorkoutDisplayPlaceholder);

		Snapshot.LatestSample = MakeFullSample();
		Display = MakeWorkoutDisplay(Snapshot);
		EXPECT_TRUE(Display.Banner == "Disconnected - showing last values");
	}

	void workout_display_reflects_device_paused_and_resting()
	{
		FWorkoutSnapshot Snapshot = MakeActiveSnapshot();
		Snapshot.LatestSample->WorkoutState = ERowingWorkoutState::Paused;
		EXPECT_TRUE(MakeWorkoutDisplay(Snapshot).Phase == EWorkoutDisplayPhase::Paused);
		Snapshot.LatestSample->WorkoutState = ERowingWorkoutState::Resting;
		EXPECT_TRUE(MakeWorkoutDisplay(Snapshot).Phase == EWorkoutDisplayPhase::Resting);
	}
} // namespace

int main()
{
	workout_display_formats_active_snapshot();
	workout_display_uses_placeholder_for_absent_metrics();
	workout_display_falls_back_to_average_power_when_no_stroke_power();
	workout_display_marks_values_stale_when_input_frozen();
	workout_display_omits_heart_rate_when_not_supplied();
	workout_display_reports_journal_unhealthy();
	workout_display_formats_pace_and_time_boundaries();
	workout_display_shows_final_values_after_session_ends();
	workout_display_reflects_device_paused_and_resting();
	workout_display_shows_waiting_when_device_is_waiting_to_begin();
	workout_display_disconnected_banner_does_not_claim_values_without_a_sample();

	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
