#include "pm5_sim/TelemetryFixtures.h"

#include <algorithm>
#include <optional>
#include <utility>

namespace pm5_sim
{
	namespace
	{
		constexpr std::uint64_t FrameIntervalMs = 100;

		FReplayTelemetryFrame
		MakeFrame(std::uint64_t ElapsedMs,
				  std::uint64_t DistanceMm,
				  std::optional<std::uint32_t> SpeedMmPerS,
				  ERowingWorkoutState WorkoutState,
				  ERowingState RowingState,
				  ERowingStrokeState StrokeState,
				  FRowingQualityFlags QualityFlags =
					  ToRowingQualityFlags(ERowingQualityFlag::None))
		{
			FRowingMetricSample Sample;
			Sample.SourceElapsedMs = ElapsedMs;
			Sample.DistanceMm = DistanceMm;
			Sample.SpeedMmPerS = SpeedMmPerS;
			Sample.WorkoutState = WorkoutState;
			Sample.RowingState = RowingState;
			Sample.StrokeState = StrokeState;
			Sample.QualityFlags = QualityFlags;
			if (SpeedMmPerS && *SpeedMmPerS != 0)
			{
				Sample.PaceMsPer500M = 500000000U / *SpeedMmPerS;
			}
			Sample.StrokeRateDeciSpm = RowingState == ERowingState::Active
										   ? std::optional<std::uint32_t>(240)
										   : std::nullopt;
			Sample.AveragePowerW = RowingState == ERowingState::Active
									   ? std::optional<std::uint32_t>(160)
									   : std::nullopt;
			Sample.StrokeCount =
				RowingState == ERowingState::Active
					? std::optional<std::uint64_t>(ElapsedMs / 2500)
					: std::optional<std::uint64_t>(0);
			return FReplayTelemetryFrame{ElapsedMs * 1000000ULL,
										 std::move(Sample)};
		}

		FGoldenTelemetryFixture
		MakeLinearFixture(std::string Name,
						  std::uint64_t DurationMs,
						  std::uint64_t FinalDistanceMm,
						  std::uint32_t StrokeRateDeciSpm)
		{
			FGoldenTelemetryFixture Fixture;
			Fixture.Name = std::move(Name);
			Fixture.DurationMs = DurationMs;
			Fixture.FinalDistanceMm = FinalDistanceMm;
			for (std::uint64_t TimeMs = 0; TimeMs <= DurationMs;
				 TimeMs += FrameIntervalMs)
			{
				const std::uint64_t Distance =
					DurationMs == 0 ? FinalDistanceMm
									: FinalDistanceMm * TimeMs / DurationMs;
				const std::uint64_t PriorTimeMs =
					TimeMs >= FrameIntervalMs ? TimeMs - FrameIntervalMs : 0;
				const std::uint64_t PriorDistance =
					DurationMs == 0
						? 0
						: FinalDistanceMm * PriorTimeMs / DurationMs;
				const std::uint64_t DeltaMm = Distance - PriorDistance;
				const std::uint32_t Speed =
					TimeMs == 0 ? 0
								: static_cast<std::uint32_t>(DeltaMm * 1000 /
															 FrameIntervalMs);
				FReplayTelemetryFrame Frame = MakeFrame(
					TimeMs,
					Distance,
					TimeMs == 0 ? std::nullopt
								: std::optional<std::uint32_t>(Speed),
					ERowingWorkoutState::Active,
					TimeMs == 0 ? ERowingState::Inactive : ERowingState::Active,
					TimeMs == 0 ? ERowingStrokeState::Waiting
								: ERowingStrokeState::Recovery);
				Frame.Sample.StrokeRateDeciSpm = StrokeRateDeciSpm;
				Fixture.Frames.push_back(std::move(Frame));
			}
			return Fixture;
		}
	} // namespace

	FMockMachineScenario MakeSyntheticIndoorRowerScenario()
	{
		FMockMachineScenario Scenario;
		Scenario.Descriptor.Id = FRowingMachineId::FromPrivateAdapterValue(
			"synthetic:indoor-rower-01");
		Scenario.Descriptor.DisplayLabel = "Synthetic indoor rower A";
		Scenario.Descriptor.SignalStrengthDbm = -42;
		Scenario.Descriptor.KindHint = ERowingMachineKind::IndoorRower;
		Scenario.Info.Manufacturer = "Synthetic Fixture";
		Scenario.Info.Model = "Synthetic Monitor";
		Scenario.Info.HardwareVersion = "fixture-hw-1";
		Scenario.Info.FirmwareVersion = "fixture-fw-1";
		Scenario.Info.MachineKind = ERowingMachineKind::IndoorRower;
		Scenario.Info.CapabilityProfileVersion = 1;
		Scenario.Info.SupportState = ERowingMachineSupportState::Allowed;
		return Scenario;
	}

	FGoldenTelemetryFixture MakeNoRowingFixture(std::uint64_t DurationMs)
	{
		FGoldenTelemetryFixture Fixture;
		Fixture.Name = "no_rowing";
		Fixture.DurationMs = DurationMs;
		Fixture.FinalDistanceMm = 0;
		for (std::uint64_t TimeMs = 0; TimeMs <= DurationMs;
			 TimeMs += FrameIntervalMs)
		{
			Fixture.Frames.push_back(
				MakeFrame(TimeMs,
						  0,
						  std::nullopt,
						  ERowingWorkoutState::WaitingToBegin,
						  ERowingState::Inactive,
						  ERowingStrokeState::Waiting));
		}
		return Fixture;
	}

	FGoldenTelemetryFixture MakeEasy30SecondFixture()
	{
		return MakeLinearFixture("easy_30_second", 30000, 66000, 200);
	}

	FGoldenTelemetryFixture Make500mSprintFixture()
	{
		return MakeLinearFixture("sprint_500m", 90000, 500000, 440);
	}

	FGoldenTelemetryFixture Make2000mRaceFixture()
	{
		return MakeLinearFixture("race_2000m", 450000, 2000000, 360);
	}

	FGoldenTelemetryFixture Make30MinuteSteadyStateFixture()
	{
		return MakeLinearFixture("steady_30_minute", 1800000, 6000000, 240);
	}

	FGoldenTelemetryFixture MakeIntervalFixture()
	{
		FGoldenTelemetryFixture Fixture;
		Fixture.Name = "intervals_4x500m";
		Fixture.DurationMs = 580000;
		Fixture.FinalDistanceMm = 2000000;
		std::uint64_t DistanceMm = 0;
		for (std::uint64_t TimeMs = 0; TimeMs <= Fixture.DurationMs;
			 TimeMs += FrameIntervalMs)
		{
			std::uint64_t RemainingMs = TimeMs;
			std::uint64_t ActiveMs = 0;
			bool IsRest = false;
			for (std::uint64_t Interval = 0; Interval < 4; ++Interval)
			{
				const std::uint64_t WorkDurationMs = 100000;
				if (RemainingMs < WorkDurationMs)
				{
					ActiveMs += RemainingMs;
					break;
				}
				ActiveMs += WorkDurationMs;
				RemainingMs -= WorkDurationMs;
				if (Interval < 3)
				{
					if (RemainingMs < 60000)
					{
						IsRest = true;
						break;
					}
					RemainingMs -= 60000;
				}
			}
			DistanceMm = Fixture.FinalDistanceMm * ActiveMs / (4 * 100000);
			const ERowingState State =
				IsRest ? ERowingState::Inactive : ERowingState::Active;
			Fixture.Frames.push_back(MakeFrame(
				TimeMs,
				DistanceMm,
				State == ERowingState::Active
					? std::optional<std::uint32_t>(5000)
					: std::nullopt,
				IsRest ? ERowingWorkoutState::Resting
					   : ERowingWorkoutState::Active,
				State,
				State == ERowingState::Active ? ERowingStrokeState::Drive
											  : ERowingStrokeState::Waiting));
		}
		return Fixture;
	}

	FGoldenTelemetryFixture MakeAbruptStopFixture()
	{
		FGoldenTelemetryFixture Fixture =
			MakeLinearFixture("abrupt_stop", 5000, 12000, 300);
		Fixture.DurationMs = 10000;
		return Fixture;
	}

	FGoldenTelemetryFixture MakePacketLossFixture()
	{
		FGoldenTelemetryFixture Fixture;
		Fixture.Name = "packet_loss_gap";
		Fixture.DurationMs = 4000;
		Fixture.FinalDistanceMm = 8000;
		for (std::uint64_t TimeMs = 0; TimeMs <= 1000;
			 TimeMs += FrameIntervalMs)
		{
			Fixture.Frames.push_back(MakeFrame(
				TimeMs,
				TimeMs * 2,
				TimeMs == 0 ? std::nullopt : std::optional<std::uint32_t>(2000),
				ERowingWorkoutState::Active,
				ERowingState::Active,
				ERowingStrokeState::Recovery));
		}
		for (std::uint64_t TimeMs = 3100; TimeMs <= Fixture.DurationMs;
			 TimeMs += FrameIntervalMs)
		{
			Fixture.Frames.push_back(
				MakeFrame(TimeMs,
						  TimeMs * 2,
						  2000,
						  ERowingWorkoutState::Active,
						  ERowingState::Active,
						  ERowingStrokeState::Recovery,
						  ToRowingQualityFlags(ERowingQualityFlag::SourceGap)));
		}
		return Fixture;
	}
} // namespace pm5_sim
