#include "ContentRuntime/ContentState.h"

#include <array>

namespace ContentRuntime
{
	bool ContentOperationAllowed(bool bWorkoutActive) noexcept
	{
		return !bWorkoutActive;
	}

	FContentBootSelection SelectBootContent(const std::optional<FInstalledContentRecord> &Active,
											const std::optional<FInstalledContentRecord> &LastKnownGood,
											std::int64_t NowUnixSeconds)
	{
		auto IsUsable = [NowUnixSeconds](const std::optional<FInstalledContentRecord> &Candidate)
		{
			return Candidate &&
				   (Candidate->State == EInstalledContentState::Active || Candidate->State == EInstalledContentState::LastKnownGood) &&
				   Candidate->ExpiresAtUnixSeconds > NowUnixSeconds && Candidate->FailureCategory.empty();
		};
		FContentBootSelection Selection;
		if (IsUsable(Active))
		{
			Selection.RouteId = Active->RouteId;
			Selection.MountedContent = Active;
			return Selection;
		}
		if (IsUsable(LastKnownGood))
		{
			Selection.RouteId = LastKnownGood->RouteId;
			Selection.MountedContent = LastKnownGood;
			return Selection;
		}
		Selection.HanUnavailableReason = Active && Active->ExpiresAtUnixSeconds <= NowUnixSeconds ? "content.han.expired" : "content.han.not_installed";
		return Selection;
	}

	const char *InstalledContentStateName(EInstalledContentState State) noexcept
	{
		switch (State)
		{
		case EInstalledContentState::Staged:
			return "staged";
		case EInstalledContentState::Verified:
			return "verified";
		case EInstalledContentState::Active:
			return "active";
		case EInstalledContentState::LastKnownGood:
			return "last_known_good";
		case EInstalledContentState::Withdrawn:
			return "withdrawn";
		case EInstalledContentState::Failed:
			return "failed";
		}
		return "failed";
	}

	std::optional<EInstalledContentState> ParseInstalledContentState(std::string_view Name) noexcept
	{
		constexpr std::array<std::pair<std::string_view, EInstalledContentState>, 6> States = {{
			{"staged", EInstalledContentState::Staged},
			{"verified", EInstalledContentState::Verified},
			{"active", EInstalledContentState::Active},
			{"last_known_good", EInstalledContentState::LastKnownGood},
			{"withdrawn", EInstalledContentState::Withdrawn},
			{"failed", EInstalledContentState::Failed},
		}};
		for (const auto &[StateName, State] : States)
		{
			if (Name == StateName)
				return State;
		}
		return std::nullopt;
	}
} // namespace ContentRuntime
