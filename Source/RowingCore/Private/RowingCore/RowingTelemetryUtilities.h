#pragma once

#include "RowingCore/RowingTelemetry.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// These helpers are intentionally private to RowingCore.  The public telemetry
// contract stays limited to value types; an adapter that needs equivalent
// validation must keep its own policy until an A0-reviewed public seam exists.
namespace RowingCore::Private
{
	// Canonical-unit scale factors.  They are declared here rather than
	// embedded at call sites so a conversion remains auditable and
	// overflow-checkable.
	inline constexpr std::uint64_t MillisecondsPerCentisecond = 10;
	inline constexpr std::uint64_t MillimetresPerTenthMetre = 100;
	inline constexpr std::uint32_t DeciSpmPerSpm = 10;

	// Returns false and leaves OutValue unchanged when the canonical value
	// cannot be represented in its destination integer type.
	bool TryConvertCentisecondsToMilliseconds(
		std::uint64_t Centiseconds, std::uint64_t &OutMilliseconds) noexcept;
	bool
	TryConvertTenthsMetresToMillimetres(std::uint64_t TenthsMetres,
										std::uint64_t &OutMillimetres) noexcept;
	bool TryConvertSpmToDeciSpm(std::uint32_t Spm,
								std::uint32_t &OutDeciSpm) noexcept;

	// The inputs are already hardware-neutral numeric values.  Vendor codecs
	// own the mapping from a vendor enum to these values.  Any unrecognised
	// value fails closed to Unknown, rather than being reinterpreted by a
	// consumer.
	ERowingWorkoutState NormalizeWorkoutState(std::uint8_t Value) noexcept;
	ERowingState NormalizeRowingState(std::uint8_t Value) noexcept;
	ERowingStrokeState NormalizeStrokeState(std::uint8_t Value) noexcept;

	constexpr FRowingQualityFlags
	AccumulateQualityFlag(FRowingQualityFlags Existing,
						  ERowingQualityFlag Additional) noexcept
	{
		return Existing | Additional;
	}

	struct FTelemetryValidationOptions
	{
		// Disabled when absent.  The caller supplies a threshold from its
		// approved capability/liveness policy; RowingCore supplies no hidden
		// cadence or stale-data tuning constant.
		std::optional<std::uint64_t> SourceGapThresholdMs;
	};

	// Maintains high-water marks only.  A time or distance regression is marked
	// on the returned immutable sample but never causes a later valid sample to
	// be compared against the regressed value.  This helper does not correct,
	// interpolate, smooth, or otherwise alter a PM measurement.
	class FRowingTelemetryValidator final
	{
	  public:
		explicit FRowingTelemetryValidator(
			FTelemetryValidationOptions Options = {}) noexcept;

		FRowingMetricSample
		ValidateAndAccumulate(const FRowingMetricSample &Sample) noexcept;
		void Reset() noexcept;

	  private:
		FTelemetryValidationOptions ValidationOptions;
		std::optional<std::uint64_t> LastSourceElapsedMs;
		std::optional<std::uint64_t> LastDistanceMm;
	};

	// TUI-facing helpers deliberately handle only optional numeric facts and a
	// stable, non-sensitive category.  Missing is rendered distinctly from
	// zero. Arbitrary adapter/system text is never passed through to
	// diagnostics.
	std::string FormatOptionalUnsigned(std::optional<std::uint64_t> Value);
	std::string FormatRedactedDiagnostic(
		std::string_view StableCategory,
		std::optional<std::uint64_t> ExpectedValue = std::nullopt,
		std::optional<std::uint64_t> ActualValue = std::nullopt);
} // namespace RowingCore::Private
