#include "RowingCore/RowingTelemetryUtilities.h"

#include <cstdint>
#include <optional>
#include <string>

namespace RowingCore::Private
{
	bool TryConvertCentisecondsToMilliseconds(
		std::uint64_t Centiseconds, std::uint64_t &OutMilliseconds) noexcept
	{
		if (Centiseconds > std::numeric_limits<std::uint64_t>::max() / 10)
		{
			return false;
		}

		OutMilliseconds = Centiseconds * 10;
		return true;
	}

	bool TryConvertTenthsMetresToMillimetres(
		std::uint64_t TenthsMetres, std::uint64_t &OutMillimetres) noexcept
	{
		if (TenthsMetres > std::numeric_limits<std::uint64_t>::max() / 100)
		{
			return false;
		}

		OutMillimetres = TenthsMetres * 100;
		return true;
	}

	bool TryConvertSpmToDeciSpm(std::uint32_t Spm,
								std::uint32_t &OutDeciSpm) noexcept
	{
		if (Spm > std::numeric_limits<std::uint32_t>::max() / 10)
		{
			return false;
		}

		OutDeciSpm = Spm * 10;
		return true;
	}

	ERowingWorkoutState NormalizeWorkoutState(std::uint8_t Value) noexcept
	{
		switch (Value)
		{
		case 0:
			return ERowingWorkoutState::Unknown;
		case 1:
			return ERowingWorkoutState::WaitingToBegin;
		case 2:
			return ERowingWorkoutState::Active;
		case 3:
			return ERowingWorkoutState::Paused;
		case 4:
			return ERowingWorkoutState::Resting;
		case 5:
			return ERowingWorkoutState::Complete;
		case 6:
			return ERowingWorkoutState::Terminated;
		default:
			return ERowingWorkoutState::Unknown;
		}
	}

	ERowingState NormalizeRowingState(std::uint8_t Value) noexcept
	{
		switch (Value)
		{
		case 0:
			return ERowingState::Unknown;
		case 1:
			return ERowingState::Inactive;
		case 2:
			return ERowingState::Active;
		default:
			return ERowingState::Unknown;
		}
	}

	ERowingStrokeState NormalizeStrokeState(std::uint8_t Value) noexcept
	{
		switch (Value)
		{
		case 0:
			return ERowingStrokeState::Unknown;
		case 1:
			return ERowingStrokeState::Waiting;
		case 2:
			return ERowingStrokeState::Drive;
		case 3:
			return ERowingStrokeState::Dwell;
		case 4:
			return ERowingStrokeState::Recovery;
		default:
			return ERowingStrokeState::Unknown;
		}
	}

	FRowingTelemetryValidator::FRowingTelemetryValidator(
		FTelemetryValidationOptions Options) noexcept
		: ValidationOptions(std::move(Options))
	{
	}

	FRowingMetricSample FRowingTelemetryValidator::ValidateAndAccumulate(
		const FRowingMetricSample &Sample) noexcept
	{
		FRowingMetricSample Result = Sample;

		if (LastSourceElapsedMs)
		{
			if (Sample.SourceElapsedMs < *LastSourceElapsedMs)
			{
				Result.QualityFlags |= ToRowingQualityFlags(ERowingQualityFlag::TimeRegression);
			}

			if (ValidationOptions.SourceGapThresholdMs &&
				Sample.SourceElapsedMs - *LastSourceElapsedMs >
					*ValidationOptions.SourceGapThresholdMs)
			{
				Result.QualityFlags |= ToRowingQualityFlags(ERowingQualityFlag::SourceGap);
			}
		}

		if (LastDistanceMm)
		{
			if (Sample.DistanceMm < *LastDistanceMm)
			{
				Result.QualityFlags |= ToRowingQualityFlags(ERowingQualityFlag::DistanceRegression);
			}
		}

		LastSourceElapsedMs = Sample.SourceElapsedMs;
		LastDistanceMm = Sample.DistanceMm;

		return Result;
	}

	void FRowingTelemetryValidator::Reset() noexcept
	{
		LastSourceElapsedMs.reset();
		LastDistanceMm.reset();
	}

	std::string FormatOptionalUnsigned(std::optional<std::uint64_t> Value)
	{
		if (Value)
		{
			return std::to_string(*Value);
		}
		else
		{
			// Return Unicode "EM DASH" character as a replacement for nullopt
			return "\xE2\x80\x94";
		}
	}

	std::string FormatRedactedDiagnostic(
		std::string_view StableCategory,
		std::optional<std::uint64_t> ExpectedValue,
		std::optional<std::uint64_t> ActualValue)
	{
		if (StableCategory.find("serial") != std::string_view::npos)
		{
			return "redacted";
		}

		std::string Result = std::string(StableCategory);
		if (ExpectedValue)
		{
			Result += " expected=" + std::to_string(*ExpectedValue);
		}
		if (ActualValue)
		{
			Result += " actual=" + std::to_string(*ActualValue);
		}
		return Result;
	}

} // namespace RowingCore::Private