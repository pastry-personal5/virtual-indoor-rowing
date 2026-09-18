#include "LocalData/TelemetryWireMapping.h"

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

	using LocalData::Private::FWireMappingError;

	FRowingMetricSample MakeFullSample()
	{
		FRowingMetricSample Sample;
		Sample.Sequence = 42;
		Sample.SourceElapsedMs = 123'456;
		Sample.ReceivedMonotonicNs = 9'876'543'210ULL;
		Sample.DistanceMm = 5'000'123;
		Sample.SpeedMmPerS = 4'200;
		Sample.PaceMsPer500M = 119'000;
		Sample.StrokeRateDeciSpm = 240;
		Sample.StrokePowerW = 210;
		Sample.AveragePowerW = 190;
		Sample.Calories = 55;
		Sample.HeartRateBpm = 150;
		Sample.DragFactor = 110;
		Sample.StrokeCount = 4'000'000'000ULL;
		Sample.WorkoutState = ERowingWorkoutState::Resting;
		Sample.RowingState = ERowingState::Active;
		Sample.StrokeState = ERowingStrokeState::Recovery;
		Sample.QualityFlags = ERowingQualityFlag::SourceGap | ERowingQualityFlag::DeviceReconnected;
		return Sample;
	}

	bool Equal(const FRowingMetricSample &A, const FRowingMetricSample &B)
	{
		return A.Sequence == B.Sequence && A.SourceElapsedMs == B.SourceElapsedMs && A.ReceivedMonotonicNs == B.ReceivedMonotonicNs && A.DistanceMm == B.DistanceMm && A.SpeedMmPerS == B.SpeedMmPerS && A.PaceMsPer500M == B.PaceMsPer500M && A.StrokeRateDeciSpm == B.StrokeRateDeciSpm && A.StrokePowerW == B.StrokePowerW && A.AveragePowerW == B.AveragePowerW && A.Calories == B.Calories && A.HeartRateBpm == B.HeartRateBpm && A.DragFactor == B.DragFactor && A.StrokeCount == B.StrokeCount && A.WorkoutState == B.WorkoutState && A.RowingState == B.RowingState && A.StrokeState == B.StrokeState && A.QualityFlags == B.QualityFlags;
	}

	void metric_sampled_wire_mapping_round_trips()
	{
		const FRowingMetricSample Original = MakeFullSample();
		const auto Parsed = LocalData::Private::ParseMetricSample(LocalData::Private::SerializeMetricSample(Original));
		EXPECT_TRUE(Equal(Original, Parsed));
	}

	void metric_sampled_wire_mapping_preserves_absent_optionals_and_zero_values()
	{
		FRowingMetricSample Original;
		Original.Sequence = 1;
		Original.HeartRateBpm = 0; // present-but-zero must stay distinct from absent
		const auto Parsed = LocalData::Private::ParseMetricSample(LocalData::Private::SerializeMetricSample(Original));
		EXPECT_TRUE(Equal(Original, Parsed));
		EXPECT_TRUE(Parsed.HeartRateBpm.has_value());
		EXPECT_TRUE(!Parsed.SpeedMmPerS.has_value());
		EXPECT_TRUE(!Parsed.StrokeCount.has_value());
	}

	void metric_sampled_wire_mapping_covers_every_enumerator()
	{
		for (const auto Workout : {ERowingWorkoutState::Unknown, ERowingWorkoutState::WaitingToBegin, ERowingWorkoutState::Active, ERowingWorkoutState::Paused, ERowingWorkoutState::Resting, ERowingWorkoutState::Complete, ERowingWorkoutState::Terminated})
		{
			for (const auto Stroke : {ERowingStrokeState::Unknown, ERowingStrokeState::Waiting, ERowingStrokeState::Drive, ERowingStrokeState::Dwell, ERowingStrokeState::Recovery})
			{
				for (const auto Rowing : {ERowingState::Unknown, ERowingState::Inactive, ERowingState::Active})
				{
					FRowingMetricSample Original;
					Original.WorkoutState = Workout;
					Original.StrokeState = Stroke;
					Original.RowingState = Rowing;
					const auto Parsed = LocalData::Private::ParseMetricSample(LocalData::Private::SerializeMetricSample(Original));
					EXPECT_TRUE(Equal(Original, Parsed));
				}
			}
		}
	}

	void device_capability_observed_wire_mapping_round_trips()
	{
		for (const auto Kind : {ERowingMachineKind::Unknown, ERowingMachineKind::IndoorRower, ERowingMachineKind::SkiErg, ERowingMachineKind::BikeErg})
		{
			for (const auto Support : {ERowingMachineSupportState::Allowed, ERowingMachineSupportState::Warn, ERowingMachineSupportState::Blocked})
			{
				FRowingMachineInfo Original;
				Original.Manufacturer = "Concept2";
				Original.Model = "PM5";
				Original.HardwareVersion = "633";
				Original.FirmwareVersion = "210";
				Original.MachineKind = Kind;
				Original.SupportedMetrics = ToRowingMetricSet(ERowingMetric::Distance) | ToRowingMetricSet(ERowingMetric::ProjectedWorkOther);
				Original.CapabilityProfileVersion = 7;
				Original.SupportState = Support;

				const auto Parsed = LocalData::Private::ParseMachineInfo(LocalData::Private::SerializeMachineInfo(Original));
				EXPECT_TRUE(Parsed.Manufacturer == Original.Manufacturer);
				EXPECT_TRUE(Parsed.Model == Original.Model);
				EXPECT_TRUE(Parsed.HardwareVersion == Original.HardwareVersion);
				EXPECT_TRUE(Parsed.FirmwareVersion == Original.FirmwareVersion);
				EXPECT_TRUE(Parsed.MachineKind == Original.MachineKind);
				EXPECT_TRUE(Parsed.SupportedMetrics == Original.SupportedMetrics);
				EXPECT_TRUE(Parsed.CapabilityProfileVersion == Original.CapabilityProfileVersion);
				EXPECT_TRUE(Parsed.SupportState == Original.SupportState);
			}
		}
	}

	template <typename FCallable>
	bool Throws(FCallable &&Callable)
	{
		try
		{
			Callable();
		}
		catch (const FWireMappingError &)
		{
			return true;
		}
		return false;
	}

	void wire_mapping_rejects_malformed_and_unsupported_input()
	{
		EXPECT_TRUE(Throws([]
						   { LocalData::Private::ParseMetricSample("\xff\xff\xff"); }));
		EXPECT_TRUE(Throws([]
						   { LocalData::Private::ParseMachineInfo("\xff\xff\xff"); }));
		// An empty message has contract_version 0 and UNSPECIFIED enums: never accepted.
		EXPECT_TRUE(Throws([]
						   { LocalData::Private::ParseMetricSample(""); }));
		EXPECT_TRUE(Throws([]
						   { LocalData::Private::ParseMachineInfo(""); }));
	}
} // namespace

int main()
{
	metric_sampled_wire_mapping_round_trips();
	metric_sampled_wire_mapping_preserves_absent_optionals_and_zero_values();
	metric_sampled_wire_mapping_covers_every_enumerator();
	device_capability_observed_wire_mapping_round_trips();
	wire_mapping_rejects_malformed_and_unsupported_input();

	if (Failures != 0)
	{
		std::cerr << Failures << " wire mapping assertion(s) failed\n";
		return EXIT_FAILURE;
	}
	std::cout << "local data wire mapping tests passed\n";
	return EXIT_SUCCESS;
}
