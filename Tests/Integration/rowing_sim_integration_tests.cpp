#include "RowingSim/TelemetryFixtures.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
	int Failures = 0;
	constexpr std::uint64_t FnvOffsetBasis = 14695981039346656037ULL;
	constexpr std::uint64_t FnvPrime = 1099511628211ULL;

	void Check(bool Condition, const std::string &Message)
	{
		if (!Condition)
		{
			++Failures;
			std::cerr << "FAIL: " << Message << '\n';
		}
	}

	struct FReplayResult
	{
		std::uint64_t Hash = FnvOffsetBasis;
		std::uint64_t MetricCount = 0;
		std::uint64_t FinalElapsedMs = 0;
		std::uint64_t FinalDistanceMm = 0;
		bool SawRest = false;
		bool SawActive = false;
		bool SawStale = false;
		bool SawSourceGap = false;
		bool AllDistancesZero = true;
	};

	void HashU64(std::uint64_t Value, std::uint64_t &Hash)
	{
		for (unsigned Shift = 0; Shift < 64; Shift += 8)
		{
			Hash ^= (Value >> Shift) & 0xffU;
			Hash *= FnvPrime;
		}
	}

	void HashOptional(const std::optional<std::uint32_t> &Value,
					  std::uint64_t &Hash)
	{
		HashU64(Value.has_value() ? 1 : 0, Hash);
		HashU64(Value.value_or(0), Hash);
	}

	void HashSample(const FRowingMetricSample &Sample, std::uint64_t &Hash)
	{
		HashU64(Sample.Sequence, Hash);
		HashU64(Sample.SourceElapsedMs, Hash);
		HashU64(Sample.ReceivedMonotonicNs, Hash);
		HashU64(Sample.DistanceMm, Hash);
		HashOptional(Sample.SpeedMmPerS, Hash);
		HashOptional(Sample.PaceMsPer500M, Hash);
		HashOptional(Sample.StrokeRateDeciSpm, Hash);
		HashOptional(Sample.StrokePowerW, Hash);
		HashOptional(Sample.AveragePowerW, Hash);
		HashOptional(Sample.Calories, Hash);
		HashOptional(Sample.HeartRateBpm, Hash);
		HashOptional(Sample.DragFactor, Hash);
		HashU64(Sample.StrokeCount.has_value() ? 1 : 0, Hash);
		HashU64(Sample.StrokeCount.value_or(0), Hash);
		HashU64(static_cast<std::uint8_t>(Sample.WorkoutState), Hash);
		HashU64(static_cast<std::uint8_t>(Sample.RowingState), Hash);
		HashU64(static_cast<std::uint8_t>(Sample.StrokeState), Hash);
		HashU64(Sample.QualityFlags, Hash);
	}

	FReplayResult RunFixture(const RowingSim::FGoldenTelemetryFixture &Fixture)
	{
		FReplayResult Result;
		RowingSim::FReplayRowingMachine Replay(
			RowingSim::MakeSyntheticIndoorRowerScenario(), Fixture.Frames);
		Check(Replay.Connect().IsAccepted(), Fixture.Name + " connects");
		FRowingMachineEvent Event;
		while (Replay.TryPollEvent(Event))
		{
		}

		for (const auto &Frame : Fixture.Frames)
		{
			Check(Replay.AdvanceTo(Frame.AtNs),
				  Fixture.Name + " advances deterministically");
			while (Replay.TryPollEvent(Event))
			{
				if (const auto *Sample =
						std::get_if<FRowingMetricSample>(&Event.Payload))
				{
					HashSample(*Sample, Result.Hash);
					++Result.MetricCount;
					Result.FinalElapsedMs = Sample->SourceElapsedMs;
					Result.FinalDistanceMm = Sample->DistanceMm;
					Result.SawRest |=
						Sample->WorkoutState == ERowingWorkoutState::Resting;
					Result.SawActive |=
						Sample->RowingState == ERowingState::Active;
					Result.AllDistancesZero &= Sample->DistanceMm == 0;
					Result.SawSourceGap |= HasRowingQualityFlag(
						Sample->QualityFlags, ERowingQualityFlag::SourceGap);
				}
				if (const auto *State =
						std::get_if<FRowingConnectionStateChanged>(
							&Event.Payload))
				{
					Result.SawStale |=
						State->NewState == ERowingConnectionState::Stale;
				}
			}
		}
		Check(Replay.GetNextFrameIndex() == Fixture.Frames.size(),
			  Fixture.Name + " consumes every frame once");
		if (Fixture.DurationMs > Fixture.Frames.back().Sample.SourceElapsedMs)
		{
			Replay.AdvanceTo(Fixture.DurationMs * 1000000ULL);
			while (Replay.TryPollEvent(Event))
			{
				if (const auto *State =
						std::get_if<FRowingConnectionStateChanged>(
							&Event.Payload))
				{
					Result.SawStale |=
						State->NewState == ERowingConnectionState::Stale;
				}
			}
		}
		return Result;
	}

	void
	CheckDeterministicGolden(const RowingSim::FGoldenTelemetryFixture &Fixture)
	{
		const FReplayResult First = RunFixture(Fixture);
		const FReplayResult Second = RunFixture(Fixture);
		Check(First.MetricCount == Fixture.Frames.size(),
			  Fixture.Name + " emits one metric for each frame");
		Check(Second.MetricCount == First.MetricCount,
			  Fixture.Name + " replay count is repeatable");
		Check(Second.Hash == First.Hash,
			  Fixture.Name + " canonical telemetry hash is repeatable");
		Check(First.FinalElapsedMs ==
				  Fixture.Frames.back().Sample.SourceElapsedMs,
			  Fixture.Name + " final source time matches fixture");
		Check(First.FinalDistanceMm == Fixture.FinalDistanceMm,
			  Fixture.Name + " final distance matches fixture");
	}

	void golden_workouts_and_fault_replays()
	{
		CheckDeterministicGolden(RowingSim::MakeEasy30SecondFixture());
		CheckDeterministicGolden(RowingSim::Make500mSprintFixture());
		CheckDeterministicGolden(RowingSim::Make2000mRaceFixture());
		CheckDeterministicGolden(RowingSim::Make30MinuteSteadyStateFixture());
		CheckDeterministicGolden(RowingSim::MakeNoRowingFixture());
		CheckDeterministicGolden(RowingSim::MakeIntervalFixture());
		CheckDeterministicGolden(RowingSim::MakeAbruptStopFixture());
		CheckDeterministicGolden(RowingSim::MakePacketLossFixture());

		const auto NoRowing = RunFixture(RowingSim::MakeNoRowingFixture());
		Check(NoRowing.AllDistancesZero,
			  "no-rowing replay never invents distance");
		const auto Intervals = RunFixture(RowingSim::MakeIntervalFixture());
		Check(Intervals.SawRest && Intervals.SawActive,
			  "interval replay covers both work and rest");
		const auto AbruptStop = RunFixture(RowingSim::MakeAbruptStopFixture());
		Check(AbruptStop.SawStale,
			  "abrupt stop enters stale state after the final status");
		const auto PacketLoss = RunFixture(RowingSim::MakePacketLossFixture());
		Check(PacketLoss.SawStale, "packet-loss gap crosses stale threshold");
		Check(PacketLoss.SawSourceGap,
			  "resumed packet-loss fixture preserves source-gap quality");
	}

	void random_stroke_run_is_seeded_deterministic_and_duration_scales()
	{
		CheckDeterministicGolden(RowingSim::MakeRandomStrokeFixture(5, 42));
		CheckDeterministicGolden(RowingSim::MakeRandomStrokeFixture(1, 7));

		const auto FiveMinutes = RowingSim::MakeRandomStrokeFixture(5, 42);
		Check(FiveMinutes.DurationMs == 5ULL * 60000ULL,
			  "random-stroke fixture duration matches the requested minutes");
		Check(!FiveMinutes.Frames.empty() &&
				  FiveMinutes.Frames.back().Sample.SourceElapsedMs ==
					  FiveMinutes.DurationMs,
			  "random-stroke fixture's final frame reaches the requested duration");

		bool SawDrive = false;
		bool SawRecovery = false;
		std::uint64_t PreviousDistanceMm = 0;
		for (const auto &Frame : FiveMinutes.Frames)
		{
			Check(Frame.Sample.DistanceMm >= PreviousDistanceMm,
				  "random-stroke fixture never synthesizes backwards distance");
			PreviousDistanceMm = Frame.Sample.DistanceMm;
			SawDrive |= Frame.Sample.StrokeState == ERowingStrokeState::Drive;
			SawRecovery |=
				Frame.Sample.StrokeState == ERowingStrokeState::Recovery;
		}
		Check(SawDrive && SawRecovery,
			  "random-stroke fixture cycles through drive and recovery");

		const auto SameSeedAgain = RowingSim::MakeRandomStrokeFixture(5, 42);
		Check(SameSeedAgain.Frames.size() == FiveMinutes.Frames.size() &&
				  SameSeedAgain.FinalDistanceMm == FiveMinutes.FinalDistanceMm,
			  "random-stroke fixture is reproducible for the same seed");

		const auto DifferentSeed = RowingSim::MakeRandomStrokeFixture(5, 43);
		Check(DifferentSeed.FinalDistanceMm != FiveMinutes.FinalDistanceMm,
			  "random-stroke fixture varies with a different seed");
	}
} // namespace

int main()
{
	golden_workouts_and_fault_replays();
	random_stroke_run_is_seeded_deterministic_and_duration_scales();
	if (Failures != 0)
	{
		std::cerr << Failures << " integration assertion(s) failed\n";
		return 1;
	}
	std::cout << "RowingSim deterministic integration scenarios passed\n";
	return 0;
}
