#include "RowingCore/RowingTelemetryUtilities.h"

#include <limits>

namespace
{
	template <typename T>
	bool TryMultiply(T Left, T Right, T &OutValue) noexcept
	{
		if (Left > std::numeric_limits<T>::max() / Right)
		{
			return false;
		}

		OutValue = Left * Right;
		return true;
	}

	bool IsSafeDiagnosticCategory(std::string_view Value) noexcept
	{
		if (Value.empty() || Value.size() > 64)
		{
			return false;
		}

		for (const char Character : Value)
		{
			const bool IsUppercase = Character >= 'A' && Character <= 'Z';
			const bool IsLowercase = Character >= 'a' && Character <= 'z';
			const bool IsDigit = Character >= '0' && Character <= '9';
			if (!IsUppercase && !IsLowercase && !IsDigit && Character != '_' &&
				Character != '-' && Character != '.')
			{
				return false;
			}
		}

		return true;
	}
} // namespace

namespace RowingCore::Private
{
	bool TryConvertCentisecondsToMilliseconds(
		const std::uint64_t Centiseconds,
		std::uint64_t &OutMilliseconds) noexcept
	{
		return TryMultiply(
			Centiseconds, MillisecondsPerCentisecond, OutMilliseconds);
	}

	bool
	TryConvertTenthsMetresToMillimetres(const std::uint64_t TenthsMetres,
										std::uint64_t &OutMillimetres) noexcept
	{
		return TryMultiply(
			TenthsMetres, MillimetresPerTenthMetre, OutMillimetres);
	}

	bool TryConvertSpmToDeciSpm(const std::uint32_t Spm,
								std::uint32_t &OutDeciSpm) noexcept
	{
		return TryMultiply(Spm, DeciSpmPerSpm, OutDeciSpm);
	}

	ERowingWorkoutState NormalizeWorkoutState(const std::uint8_t Value) noexcept
	{
		switch (Value)
		{
		case static_cast<std::uint8_t>(ERowingWorkoutState::WaitingToBegin):
			return ERowingWorkoutState::WaitingToBegin;
		case static_cast<std::uint8_t>(ERowingWorkoutState::Active):
			return ERowingWorkoutState::Active;
		case static_cast<std::uint8_t>(ERowingWorkoutState::Paused):
			return ERowingWorkoutState::Paused;
		case static_cast<std::uint8_t>(ERowingWorkoutState::Resting):
			return ERowingWorkoutState::Resting;
		case static_cast<std::uint8_t>(ERowingWorkoutState::Complete):
			return ERowingWorkoutState::Complete;
		case static_cast<std::uint8_t>(ERowingWorkoutState::Terminated):
			return ERowingWorkoutState::Terminated;
		case static_cast<std::uint8_t>(ERowingWorkoutState::Unknown):
		default:
			return ERowingWorkoutState::Unknown;
		}
	}

	ERowingState NormalizeRowingState(const std::uint8_t Value) noexcept
	{
		switch (Value)
		{
		case static_cast<std::uint8_t>(ERowingState::Inactive):
			return ERowingState::Inactive;
		case static_cast<std::uint8_t>(ERowingState::Active):
			return ERowingState::Active;
		case static_cast<std::uint8_t>(ERowingState::Unknown):
		default:
			return ERowingState::Unknown;
		}
	}

	ERowingStrokeState NormalizeStrokeState(const std::uint8_t Value) noexcept
	{
		switch (Value)
		{
		case static_cast<std::uint8_t>(ERowingStrokeState::Waiting):
			return ERowingStrokeState::Waiting;
		case static_cast<std::uint8_t>(ERowingStrokeState::Drive):
			return ERowingStrokeState::Drive;
		case static_cast<std::uint8_t>(ERowingStrokeState::Dwell):
			return ERowingStrokeState::Dwell;
		case static_cast<std::uint8_t>(ERowingStrokeState::Recovery):
			return ERowingStrokeState::Recovery;
		case static_cast<std::uint8_t>(ERowingStrokeState::Unknown):
		default:
			return ERowingStrokeState::Unknown;
		}
	}

	FRowingTelemetryValidator::FRowingTelemetryValidator(
		FTelemetryValidationOptions Options) noexcept
		: ValidationOptions(Options)
	{
	}

	FRowingMetricSample FRowingTelemetryValidator::ValidateAndAccumulate(
		const FRowingMetricSample &Sample) noexcept
	{
		FRowingMetricSample Validated = Sample;

		if (LastSourceElapsedMs.has_value())
		{
			if (Validated.SourceElapsedMs < *LastSourceElapsedMs)
			{
				Validated.QualityFlags = AccumulateQualityFlag(
					Validated.QualityFlags, ERowingQualityFlag::TimeRegression);
			}
			else
			{
				const std::uint64_t ElapsedAdvanceMs =
					Validated.SourceElapsedMs - *LastSourceElapsedMs;
				if (ValidationOptions.SourceGapThresholdMs.has_value() &&
					ElapsedAdvanceMs > *ValidationOptions.SourceGapThresholdMs)
				{
					Validated.QualityFlags = AccumulateQualityFlag(
						Validated.QualityFlags, ERowingQualityFlag::SourceGap);
				}
				LastSourceElapsedMs = Validated.SourceElapsedMs;
			}
		}
		else
		{
			LastSourceElapsedMs = Validated.SourceElapsedMs;
		}

		if (LastDistanceMm.has_value())
		{
			if (Validated.DistanceMm < *LastDistanceMm)
			{
				Validated.QualityFlags = AccumulateQualityFlag(
					Validated.QualityFlags,
					ERowingQualityFlag::DistanceRegression);
			}
			else
			{
				LastDistanceMm = Validated.DistanceMm;
			}
		}
		else
		{
			LastDistanceMm = Validated.DistanceMm;
		}

		return Validated;
	}

	void FRowingTelemetryValidator::Reset() noexcept
	{
		LastSourceElapsedMs.reset();
		LastDistanceMm.reset();
	}

	std::string FormatOptionalUnsigned(const std::optional<std::uint64_t> Value)
	{
		return Value.has_value() ? std::to_string(*Value) : "\xE2\x80\x94";
	}

	std::string
	FormatRedactedDiagnostic(const std::string_view StableCategory,
							 const std::optional<std::uint64_t> ExpectedValue,
							 const std::optional<std::uint64_t> ActualValue)
	{
		std::string Formatted = IsSafeDiagnosticCategory(StableCategory)
									? std::string(StableCategory)
									: "redacted";
		if (ExpectedValue.has_value())
		{
			Formatted += " expected=" + std::to_string(*ExpectedValue);
		}
		if (ActualValue.has_value())
		{
			Formatted += " actual=" + std::to_string(*ActualValue);
		}
		return Formatted;
	}
} // namespace RowingCore::Private
