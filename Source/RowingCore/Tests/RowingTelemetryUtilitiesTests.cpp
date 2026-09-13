#include "RowingCore/RowingTelemetryUtilities.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <string>

namespace
{
	int Failures = 0;

	void Expect(const bool Condition,
				const char *const Expression,
				const char *const TestName)
	{
		if (!Condition)
		{
			std::cerr << TestName << ": expectation failed: " << Expression
					  << '\n';
			++Failures;
		}
	}

#define EXPECT_TRUE(Expression) Expect((Expression), #Expression, __func__)

	void exact_fixed_scale_conversions_preserve_units()
	{
		std::uint64_t Milliseconds = 0;
		std::uint64_t Millimetres = 0;
		std::uint32_t DeciSpm = 0;

		EXPECT_TRUE(RowingCore::Private::TryConvertCentisecondsToMilliseconds(
			123, Milliseconds));
		EXPECT_TRUE(Milliseconds == 1230);
		EXPECT_TRUE(RowingCore::Private::TryConvertTenthsMetresToMillimetres(
			42, Millimetres));
		EXPECT_TRUE(Millimetres == 4200);
		EXPECT_TRUE(RowingCore::Private::TryConvertSpmToDeciSpm(31, DeciSpm));
		EXPECT_TRUE(DeciSpm == 310);
	}

	void overflowing_conversions_fail_without_replacing_destination()
	{
		std::uint64_t Milliseconds = 77;
		std::uint64_t Millimetres = 88;
		std::uint32_t DeciSpm = 99;

		EXPECT_TRUE(!RowingCore::Private::TryConvertCentisecondsToMilliseconds(
			std::numeric_limits<std::uint64_t>::max(), Milliseconds));
		EXPECT_TRUE(Milliseconds == 77);
		EXPECT_TRUE(!RowingCore::Private::TryConvertTenthsMetresToMillimetres(
			std::numeric_limits<std::uint64_t>::max(), Millimetres));
		EXPECT_TRUE(Millimetres == 88);
		EXPECT_TRUE(!RowingCore::Private::TryConvertSpmToDeciSpm(
			std::numeric_limits<std::uint32_t>::max(), DeciSpm));
		EXPECT_TRUE(DeciSpm == 99);
	}

	void unknown_normalized_state_values_fail_closed()
	{
		EXPECT_TRUE(RowingCore::Private::NormalizeWorkoutState(2) ==
					ERowingWorkoutState::Active);
		EXPECT_TRUE(RowingCore::Private::NormalizeRowingState(2) ==
					ERowingState::Active);
		EXPECT_TRUE(RowingCore::Private::NormalizeStrokeState(4) ==
					ERowingStrokeState::Recovery);
		EXPECT_TRUE(RowingCore::Private::NormalizeWorkoutState(255) ==
					ERowingWorkoutState::Unknown);
		EXPECT_TRUE(RowingCore::Private::NormalizeRowingState(3) ==
					ERowingState::Unknown);
		EXPECT_TRUE(RowingCore::Private::NormalizeStrokeState(5) ==
					ERowingStrokeState::Unknown);
	}

	void quality_flags_accumulate_without_erasing_prior_evidence()
	{
		FRowingQualityFlags Flags =
			ToRowingQualityFlags(ERowingQualityFlag::MissingField);
		Flags = RowingCore::Private::AccumulateQualityFlag(
			Flags, ERowingQualityFlag::Outlier);
		EXPECT_TRUE(
			HasRowingQualityFlag(Flags, ERowingQualityFlag::MissingField));
		EXPECT_TRUE(HasRowingQualityFlag(Flags, ERowingQualityFlag::Outlier));
	}

	void telemetry_regressions_accumulate_without_rewriting_measurements()
	{
		RowingCore::Private::FRowingTelemetryValidator Validator;
		FRowingMetricSample First;
		First.SourceElapsedMs = 1000;
		First.DistanceMm = 5000;
		First.QualityFlags =
			ToRowingQualityFlags(ERowingQualityFlag::DeviceReconnected);
		const FRowingMetricSample FirstValidated =
			Validator.ValidateAndAccumulate(First);
		EXPECT_TRUE(FirstValidated.QualityFlags == First.QualityFlags);

		FRowingMetricSample Regressed;
		Regressed.SourceElapsedMs = 900;
		Regressed.DistanceMm = 4900;
		const FRowingMetricSample RegressedValidated =
			Validator.ValidateAndAccumulate(Regressed);
		EXPECT_TRUE(HasRowingQualityFlag(RegressedValidated.QualityFlags,
										 ERowingQualityFlag::TimeRegression));
		EXPECT_TRUE(
			HasRowingQualityFlag(RegressedValidated.QualityFlags,
								 ERowingQualityFlag::DistanceRegression));
		EXPECT_TRUE(RegressedValidated.SourceElapsedMs == 900);
		EXPECT_TRUE(RegressedValidated.DistanceMm == 4900);

		FRowingMetricSample Recovered;
		Recovered.SourceElapsedMs = 1100;
		Recovered.DistanceMm = 5100;
		const FRowingMetricSample RecoveredValidated =
			Validator.ValidateAndAccumulate(Recovered);
		EXPECT_TRUE(!HasRowingQualityFlag(RecoveredValidated.QualityFlags,
										  ERowingQualityFlag::TimeRegression));
		EXPECT_TRUE(
			!HasRowingQualityFlag(RecoveredValidated.QualityFlags,
								  ERowingQualityFlag::DistanceRegression));
	}

	void source_gap_threshold_is_explicit_and_boundary_is_not_a_gap()
	{
		RowingCore::Private::FTelemetryValidationOptions Options;
		Options.SourceGapThresholdMs = 500;
		RowingCore::Private::FRowingTelemetryValidator Validator(Options);

		FRowingMetricSample First;
		First.SourceElapsedMs = 1000;
		Validator.ValidateAndAccumulate(First);

		FRowingMetricSample AtBoundary;
		AtBoundary.SourceElapsedMs = 1500;
		EXPECT_TRUE(!HasRowingQualityFlag(
			Validator.ValidateAndAccumulate(AtBoundary).QualityFlags,
			ERowingQualityFlag::SourceGap));

		FRowingMetricSample BeyondBoundary;
		BeyondBoundary.SourceElapsedMs = 2001;
		EXPECT_TRUE(HasRowingQualityFlag(
			Validator.ValidateAndAccumulate(BeyondBoundary).QualityFlags,
			ERowingQualityFlag::SourceGap));
	}

	void reset_allows_reconnect_to_start_a_new_monotonic_epoch()
	{
		RowingCore::Private::FRowingTelemetryValidator Validator;
		FRowingMetricSample BeforeReconnect;
		BeforeReconnect.SourceElapsedMs = 9000;
		BeforeReconnect.DistanceMm = 18000;
		Validator.ValidateAndAccumulate(BeforeReconnect);

		Validator.Reset();
		FRowingMetricSample AfterReconnect;
		AfterReconnect.SourceElapsedMs = 10;
		AfterReconnect.DistanceMm = 0;
		const FRowingMetricSample Validated =
			Validator.ValidateAndAccumulate(AfterReconnect);
		EXPECT_TRUE(!HasRowingQualityFlag(Validated.QualityFlags,
										  ERowingQualityFlag::TimeRegression));
		EXPECT_TRUE(!HasRowingQualityFlag(
			Validated.QualityFlags, ERowingQualityFlag::DistanceRegression));
	}

	void formatting_preserves_absence_and_redacts_untrusted_text()
	{
		EXPECT_TRUE(RowingCore::Private::FormatOptionalUnsigned(std::nullopt) ==
					"\xE2\x80\x94");
		EXPECT_TRUE(RowingCore::Private::FormatOptionalUnsigned(0) == "0");
		EXPECT_TRUE(RowingCore::Private::FormatRedactedDiagnostic(
						"InvalidPacketLength", 20, 19) ==
					"InvalidPacketLength expected=20 actual=19");
		EXPECT_TRUE(RowingCore::Private::FormatRedactedDiagnostic(
						"serial=12345") == "redacted");
	}
} // namespace

int main()
{
	exact_fixed_scale_conversions_preserve_units();
	overflowing_conversions_fail_without_replacing_destination();
	unknown_normalized_state_values_fail_closed();
	quality_flags_accumulate_without_erasing_prior_evidence();
	telemetry_regressions_accumulate_without_rewriting_measurements();
	source_gap_threshold_is_explicit_and_boundary_is_not_a_gap();
	reset_allows_reconnect_to_start_a_new_monotonic_epoch();
	formatting_preserves_absence_and_redacts_untrusted_text();

	return Failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
