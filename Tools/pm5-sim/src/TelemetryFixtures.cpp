#include "pm5_sim/TelemetryFixtures.h"

#include <algorithm>
#include <optional>
#include <random>
#include <string>
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

	FGoldenTelemetryFixture MakeRandomStrokeFixture(std::uint64_t DurationMinutes,
													std::uint32_t Seed)
	{
		const std::uint64_t TotalDurationMs = DurationMinutes * 60000ULL;
		if (TotalDurationMs == 0)
			return MakeNoRowingFixture(0);

		struct FStroke
		{
			std::uint64_t StartMs = 0;
			std::uint64_t DurationMs = 0;
			std::uint64_t DriveDurationMs = 0;
			std::uint64_t StartDistanceMm = 0;
			std::uint64_t DistanceMm = 0;
			std::uint32_t StrokeRateDeciSpm = 0;
			std::uint32_t PowerW = 0;
		};

		std::mt19937 Rng(Seed);
		std::uniform_int_distribution<std::uint32_t> StrokeRateDist(180, 320);		  // 18.0-32.0 spm
		std::uniform_int_distribution<std::uint32_t> PowerDist(120, 220);			  // watts
		std::uniform_int_distribution<std::uint32_t> StrokeDistanceDist(8000, 11000); // mm/stroke
		std::uniform_real_distribution<double> DriveFractionDist(0.33, 0.45);

		std::vector<FStroke> Strokes;
		std::uint64_t ElapsedMs = 0;
		std::uint64_t CumulativeDistanceMm = 0;
		while (ElapsedMs < TotalDurationMs)
		{
			FStroke Stroke;
			Stroke.StrokeRateDeciSpm = StrokeRateDist(Rng);
			Stroke.PowerW = PowerDist(Rng);
			Stroke.DurationMs = 600000ULL / Stroke.StrokeRateDeciSpm;
			Stroke.DriveDurationMs = static_cast<std::uint64_t>(
				static_cast<double>(Stroke.DurationMs) * DriveFractionDist(Rng));
			Stroke.StartMs = ElapsedMs;
			Stroke.StartDistanceMm = CumulativeDistanceMm;
			Stroke.DistanceMm = StrokeDistanceDist(Rng);
			CumulativeDistanceMm += Stroke.DistanceMm;
			Strokes.push_back(Stroke);
			ElapsedMs += Stroke.DurationMs;
		}

		FGoldenTelemetryFixture Fixture;
		Fixture.Name = "random_stroke_" + std::to_string(DurationMinutes) + "min_seed" +
					   std::to_string(Seed);
		Fixture.DurationMs = TotalDurationMs;

		std::size_t CurrentStrokeIndex = 0;
		for (std::uint64_t TimeMs = 0; TimeMs <= TotalDurationMs;
			 TimeMs += FrameIntervalMs)
		{
			while (CurrentStrokeIndex + 1 < Strokes.size() &&
				   TimeMs >= Strokes[CurrentStrokeIndex].StartMs +
								 Strokes[CurrentStrokeIndex].DurationMs)
			{
				++CurrentStrokeIndex;
			}
			const FStroke &Stroke = Strokes[CurrentStrokeIndex];
			const std::uint64_t TimeIntoStroke =
				TimeMs > Stroke.StartMs ? TimeMs - Stroke.StartMs : 0;
			const std::uint64_t ClampedTimeIntoStroke =
				std::min(TimeIntoStroke, Stroke.DurationMs);
			const std::uint64_t DistanceMm =
				Stroke.StartDistanceMm +
				Stroke.DistanceMm * ClampedTimeIntoStroke /
					std::max<std::uint64_t>(Stroke.DurationMs, 1);
			const bool InDrive = ClampedTimeIntoStroke < Stroke.DriveDurationMs;

			FRowingMetricSample Sample;
			Sample.SourceElapsedMs = TimeMs;
			Sample.DistanceMm = DistanceMm;
			Sample.SpeedMmPerS =
				TimeMs == 0
					? std::nullopt
					: std::optional<std::uint32_t>(static_cast<std::uint32_t>(
						  Stroke.DistanceMm * 1000 / Stroke.DurationMs));
			if (Sample.SpeedMmPerS && *Sample.SpeedMmPerS != 0)
				Sample.PaceMsPer500M = 500000000U / *Sample.SpeedMmPerS;
			Sample.WorkoutState = ERowingWorkoutState::Active;
			Sample.RowingState =
				TimeMs == 0 ? ERowingState::Inactive : ERowingState::Active;
			Sample.StrokeState = TimeMs == 0 ? ERowingStrokeState::Waiting
											 : (InDrive ? ERowingStrokeState::Drive
														: ERowingStrokeState::Recovery);
			Sample.StrokeRateDeciSpm = TimeMs == 0
										   ? std::nullopt
										   : std::optional<std::uint32_t>(
												 Stroke.StrokeRateDeciSpm);
			Sample.AveragePowerW =
				TimeMs == 0 ? std::nullopt
							: std::optional<std::uint32_t>(Stroke.PowerW);
			Sample.StrokeCount = std::optional<std::uint64_t>(CurrentStrokeIndex);
			Fixture.Frames.push_back(
				FReplayTelemetryFrame{TimeMs * 1000000ULL, std::move(Sample)});
		}
		Fixture.FinalDistanceMm = Fixture.Frames.back().Sample.DistanceMm;
		return Fixture;
	}
} // namespace pm5_sim
