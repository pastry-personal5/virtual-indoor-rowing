#pragma once

#include "ContentRuntime/ContentManifest.h"

#include <cstdint>
#include <optional>
#include <string>

namespace ContentRuntime
{
	enum class EInstalledContentState : std::uint8_t
	{
		Staged,
		Verified,
		Active,
		LastKnownGood,
		Withdrawn,
		Failed
	};

	struct FInstalledContentRecord
	{
		std::string ContentSetId;
		std::string RouteId;
		std::string SemanticVersion;
		std::string ManifestHashHex;
		EInstalledContentState State = EInstalledContentState::Staged;
		std::uint64_t CatalogRevision = 0;
		std::int64_t IssuedAtUnixSeconds = 0;
		std::int64_t ExpiresAtUnixSeconds = 0;
		std::string InstallPath;
		std::string FailureCategory;
	};

	struct FContentBootSelection
	{
		std::string RouteId = "route.standard.2k";
		std::optional<FInstalledContentRecord> MountedContent;
		std::string HanUnavailableReason;
	};

	bool ContentOperationAllowed(bool bWorkoutActive) noexcept;
	FContentBootSelection SelectBootContent(const std::optional<FInstalledContentRecord> &Active,
											const std::optional<FInstalledContentRecord> &LastKnownGood,
											std::int64_t NowUnixSeconds);
	const char *InstalledContentStateName(EInstalledContentState State) noexcept;
	std::optional<EInstalledContentState> ParseInstalledContentState(std::string_view Name) noexcept;
} // namespace ContentRuntime
